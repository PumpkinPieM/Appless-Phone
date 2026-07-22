#include "lite_runtime.h"

#include "lite_vendor_adapter.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace {
enum class RuntimeState {
    Unavailable,
    Uninitialized,
    Loading,
    Ready,
    Generating,
    Failed,
    Releasing,
    Released
};

bool IsValidUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = bytes[index];
        std::size_t continuation = 0;
        std::uint32_t codePoint = 0;
        if (first <= 0x7f) {
            ++index;
            continue;
        } else if ((first & 0xe0) == 0xc0) {
            continuation = 1;
            codePoint = first & 0x1f;
            if (codePoint == 0) return false;
        } else if ((first & 0xf0) == 0xe0) {
            continuation = 2;
            codePoint = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            continuation = 3;
            codePoint = first & 0x07;
        } else {
            return false;
        }
        if (index + continuation >= value.size()) return false;
        for (std::size_t offset = 1; offset <= continuation; ++offset) {
            const unsigned char next = bytes[index + offset];
            if ((next & 0xc0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if ((continuation == 1 && codePoint < 0x80) ||
            (continuation == 2 && codePoint < 0x800) ||
            (continuation == 3 && codePoint < 0x10000) ||
            codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
            return false;
        }
        index += continuation + 1;
    }
    return true;
}
} // namespace

struct LiteRuntime::Impl {
    std::mutex mutex;
    std::condition_variable condition;
    std::queue<std::function<void()>> jobs;
    bool stopping = false;
    std::thread worker;
    std::unique_ptr<LiteVendorAdapter> adapter = CreateLiteVendorAdapter();
    RuntimeState state = IsLiteVendorAvailable() ? RuntimeState::Uninitialized : RuntimeState::Unavailable;
    std::string configPath;

    Impl() : worker([this]() { WorkerLoop(); }) {}

    void Enqueue(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) {
                throw LiteRuntimeError("BACKEND_RELEASED", "The native runtime is shutting down.");
            }
            jobs.push(std::move(job));
        }
        condition.notify_one();
    }

    void WorkerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]() { return stopping || !jobs.empty(); });
                if (stopping && jobs.empty()) break;
                job = std::move(jobs.front());
                jobs.pop();
            }
            job();
        }
        try {
            adapter->Release();
        } catch (...) {
        }
        state = RuntimeState::Released;
    }
};

LiteRuntime& LiteRuntime::Instance() {
    static LiteRuntime runtime;
    return runtime;
}

LiteRuntime::LiteRuntime() : impl_(std::make_unique<Impl>()) {}

LiteRuntime::~LiteRuntime() {
    try {
        Release().get();
    } catch (...) {
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->condition.notify_one();
    if (impl_->worker.joinable()) impl_->worker.join();
}

std::future<void> LiteRuntime::Initialize(const std::string& configPath) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    try {
        impl_->Enqueue([this, promise, configPath]() {
            try {
                if (impl_->state == RuntimeState::Releasing) {
                    throw LiteRuntimeError("BACKEND_RELEASED", "The native runtime is being released.");
                }
                if (impl_->state == RuntimeState::Released) {
                    // A released ArkTS backend remains unusable, but a newly-created backend may
                    // begin a fresh process-level lifecycle (for example after an explicit reload).
                    impl_->state = IsLiteVendorAvailable() ? RuntimeState::Uninitialized : RuntimeState::Unavailable;
                }
                if (impl_->state == RuntimeState::Unavailable) {
                    throw LiteRuntimeError("NATIVE_UNAVAILABLE", "The vendor library is unavailable for this ABI.");
                }
                if (impl_->state == RuntimeState::Ready) {
                    if (impl_->configPath == configPath) {
                        promise->set_value();
                        return;
                    }
                    throw LiteRuntimeError("MODEL_LOAD_FAILED", "A different model is already loaded.");
                }
                impl_->state = RuntimeState::Loading;
                try {
                    impl_->adapter->Initialize(configPath);
                    impl_->configPath = configPath;
                    impl_->state = RuntimeState::Ready;
                    promise->set_value();
                } catch (...) {
                    impl_->state = RuntimeState::Failed;
                    throw;
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
    return future;
}

std::future<std::string> LiteRuntime::Generate(const std::string& prompt) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    try {
        impl_->Enqueue([this, promise, prompt]() {
            try {
                if (impl_->state == RuntimeState::Released || impl_->state == RuntimeState::Releasing) {
                    throw LiteRuntimeError("BACKEND_RELEASED", "The native runtime has been released.");
                }
                if (impl_->state != RuntimeState::Ready) {
                    throw LiteRuntimeError("MODEL_NOT_INITIALIZED", "The native model is not initialized.");
                }
                impl_->state = RuntimeState::Generating;
                try {
                    std::string output = impl_->adapter->Generate(prompt);
                    impl_->state = RuntimeState::Ready;
                    if (output.empty()) {
                        throw LiteRuntimeError("EMPTY_MODEL_RESPONSE", "Vendor generation returned an empty response.");
                    }
                    if (!IsValidUtf8(output)) {
                        throw LiteRuntimeError("GENERATION_FAILED", "Vendor generation returned invalid UTF-8.");
                    }
                    promise->set_value(std::move(output));
                } catch (...) {
                    impl_->state = RuntimeState::Ready;
                    throw;
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
    return future;
}

std::future<void> LiteRuntime::Release() {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    try {
        impl_->Enqueue([this, promise]() {
            try {
                if (impl_->state == RuntimeState::Released) {
                    promise->set_value();
                    return;
                }
                impl_->state = RuntimeState::Releasing;
                impl_->adapter->Release();
                impl_->configPath.clear();
                impl_->state = RuntimeState::Released;
                promise->set_value();
            } catch (...) {
                impl_->state = RuntimeState::Released;
                promise->set_exception(std::current_exception());
            }
        });
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
    return future;
}

bool LiteRuntime::IsVendorAvailable() const {
    return IsLiteVendorAvailable();
}
