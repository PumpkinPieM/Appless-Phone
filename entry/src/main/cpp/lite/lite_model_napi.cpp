#include "napi/native_api.h"

#ifdef LITE_LLM_VENDOR_AVAILABLE
#include "lite_llm.h"
#endif

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
enum class Operation { Initialize, Generate, Release };

class NativeError final : public std::runtime_error {
public:
    NativeError(std::string code, const std::string& message)
        : std::runtime_error(message), code_(std::move(code)) {}

    const std::string& Code() const noexcept { return code_; }

private:
    std::string code_;
};

struct WorkData {
    napi_deferred deferred = nullptr;
    napi_async_work work = nullptr;
    Operation operation = Operation::Initialize;
    std::string input;
    std::string output;
    std::string errorCode;
    std::string errorMessage;
};

std::mutex modelMutex;

#ifdef LITE_LLM_VENDOR_AVAILABLE
std::unique_ptr<lite_llm::LiteLlm> model;
std::string loadedConfigPath;
#endif

bool ReadString(napi_env env, napi_value value, std::string& output) {
    napi_valuetype type;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) return false;
    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) return false;
    std::vector<char> buffer(length + 1, '\0');
    std::size_t written = 0;
    if (napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &written) != napi_ok) return false;
    output.assign(buffer.data(), written);
    return true;
}

void InitializeModel(const std::string& configPath) {
#ifdef LITE_LLM_VENDOR_AVAILABLE
    if (model && loadedConfigPath == configPath) return;
    auto nextModel = lite_llm::LiteLlm::CreateFromConfig(configPath);
    if (!nextModel) {
        throw NativeError("MODEL_LOAD_FAILED", "Vendor model creation returned null.");
    }
    model = std::move(nextModel);
    loadedConfigPath = configPath;
#else
    (void)configPath;
    throw NativeError("NATIVE_UNAVAILABLE", "The vendor header/library was not compiled for this ABI.");
#endif
}

std::string GenerateText(const std::string& prompt) {
#ifdef LITE_LLM_VENDOR_AVAILABLE
    if (!model) {
        throw NativeError("MODEL_NOT_INITIALIZED", "initialize must complete before generation.");
    }
    const std::string output = model->Generate(prompt);
    if (output.empty()) {
        throw NativeError("EMPTY_MODEL_RESPONSE", "Vendor generation returned an empty response.");
    }
    return output;
#else
    (void)prompt;
    throw NativeError("NATIVE_UNAVAILABLE", "The vendor header/library was not compiled for this ABI.");
#endif
}

void ReleaseModel() {
#ifdef LITE_LLM_VENDOR_AVAILABLE
    model.reset();
    loadedConfigPath.clear();
#endif
}

void Execute(napi_env, void* rawData) {
    auto* data = static_cast<WorkData*>(rawData);
    if (!data->errorCode.empty()) return;
    try {
        std::lock_guard<std::mutex> lock(modelMutex);
        if (data->operation == Operation::Initialize) {
            InitializeModel(data->input);
        } else if (data->operation == Operation::Generate) {
            data->output = GenerateText(data->input);
        } else {
            ReleaseModel();
        }
    } catch (const NativeError& error) {
        data->errorCode = error.Code();
        data->errorMessage = error.what();
    } catch (const std::exception& error) {
        data->errorCode = data->operation == Operation::Generate ? "GENERATION_FAILED" : "MODEL_LOAD_FAILED";
        data->errorMessage = error.what();
    } catch (...) {
        data->errorCode = data->operation == Operation::Generate ? "GENERATION_FAILED" : "MODEL_LOAD_FAILED";
        data->errorMessage = "Native local-model operation failed.";
    }
}

void Complete(napi_env env, napi_status status, void* rawData) {
    std::unique_ptr<WorkData> data(static_cast<WorkData*>(rawData));
    if (status != napi_ok && data->errorCode.empty()) {
        data->errorCode = "GENERATION_FAILED";
        data->errorMessage = "Native async work did not complete.";
    }
    if (!data->errorCode.empty()) {
        const std::string formatted = "[" + data->errorCode + "] " + data->errorMessage;
        napi_value message;
        napi_value error;
        napi_value code;
        napi_create_string_utf8(env, formatted.c_str(), formatted.size(), &message);
        napi_create_error(env, nullptr, message, &error);
        napi_create_string_utf8(env, data->errorCode.c_str(), data->errorCode.size(), &code);
        napi_set_named_property(env, error, "code", code);
        napi_reject_deferred(env, data->deferred, error);
    } else if (data->operation == Operation::Generate) {
        napi_value output;
        napi_create_string_utf8(env, data->output.c_str(), data->output.size(), &output);
        napi_resolve_deferred(env, data->deferred, output);
    } else {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        napi_resolve_deferred(env, data->deferred, undefined);
    }
    napi_delete_async_work(env, data->work);
}

napi_value Queue(napi_env env, napi_callback_info info, Operation operation) {
    auto data = std::make_unique<WorkData>();
    data->operation = operation;

    napi_value promise;
    napi_create_promise(env, &data->deferred, &promise);
    if (operation != Operation::Release) {
        std::size_t argc = 1;
        napi_value args[1] = {nullptr};
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc != 1 || !ReadString(env, args[0], data->input) || data->input.empty()) {
            data->errorCode = operation == Operation::Initialize ? "RUNTIME_ROOT_INVALID" : "GENERATION_FAILED";
            data->errorMessage = "Expected one non-empty string argument.";
        }
    }

    napi_value resourceName;
    const char* name = operation == Operation::Generate ? "LiteModelGenerate" :
        (operation == Operation::Initialize ? "LiteModelInitialize" : "LiteModelRelease");
    napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &resourceName);
    WorkData* raw = data.release();
    if (napi_create_async_work(env, nullptr, resourceName, Execute, Complete, raw, &raw->work) != napi_ok ||
        napi_queue_async_work(env, raw->work) != napi_ok) {
        raw->errorCode = "GENERATION_FAILED";
        raw->errorMessage = "Unable to queue native async work.";
        Complete(env, napi_ok, raw);
    }
    return promise;
}

napi_value Initialize(napi_env env, napi_callback_info info) {
    return Queue(env, info, Operation::Initialize);
}

napi_value Generate(napi_env env, napi_callback_info info) {
    return Queue(env, info, Operation::Generate);
}

napi_value Release(napi_env env, napi_callback_info info) {
    return Queue(env, info, Operation::Release);
}

napi_value IsVendorAvailable(napi_env env, napi_callback_info) {
    napi_value result;
#ifdef LITE_LLM_VENDOR_AVAILABLE
    napi_get_boolean(env, true, &result);
#else
    napi_get_boolean(env, false, &result);
#endif
    return result;
}
} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor descriptors[] = {
        {"initialize", nullptr, Initialize, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"generate", nullptr, Generate, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"release", nullptr, Release, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isVendorAvailable", nullptr, IsVendorAvailable, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    return exports;
}
EXTERN_C_END

static napi_module liteModelModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "lite_model_native",
    .nm_priv = nullptr,
    .reserved = {nullptr}
};

extern "C" __attribute__((constructor)) void RegisterLiteModelModule() {
    napi_module_register(&liteModelModule);
}
