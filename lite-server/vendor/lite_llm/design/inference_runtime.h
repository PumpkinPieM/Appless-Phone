#ifndef LITE_LLM_DESIGN_INFERENCE_RUNTIME_H
#define LITE_LLM_DESIGN_INFERENCE_RUNTIME_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lite_llm::design {

enum class RuntimeStatus {
    Ok,
    InvalidArgument,
    ModelLoadFailed,
    ContextTooLong,
    OutOfMemory,
    Interrupted,
    InternalError,
};

struct RuntimeResult {
    RuntimeStatus status = RuntimeStatus::InternalError;
    std::string error;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == RuntimeStatus::Ok;
    }
};

struct RuntimeConfig {
    std::string model_path;
    std::string tokenizer_path;

    std::size_t max_context_tokens = 0;
    std::size_t kv_cache_budget_bytes = 0;
    std::uint32_t worker_threads = 0;
};

enum class CachePolicy {
    Disabled,
    Auto,
};

struct GenerationOptions {
    std::uint32_t max_new_tokens = 512;
    float temperature = 0.7F;
    float top_p = 0.9F;

    // A value of zero lets the runtime use its model default.
    std::uint32_t top_k = 0;

    std::vector<std::string> stop;
    std::optional<std::uint64_t> seed;
};

struct GenerateRequest {
    // An opaque conversation identity used only for best-effort KV/prefix
    // caching. It is not a request-correlation identifier.
    std::string session_id;

    // The vendor has already applied the selected model's chat template. The
    // runtime owns tokenization so it can validate prefix-cache reuse using the
    // actual token sequence.
    std::string prompt;

    GenerationOptions options;
    CachePolicy cache_policy = CachePolicy::Auto;
};

enum class FinishReason {
    Stop,
    Length,
    Interrupted,
};

struct TokenUsage {
    std::uint32_t prompt_tokens = 0;
    std::uint32_t generated_tokens = 0;
    std::uint32_t reused_prefix_tokens = 0;
};

struct GenerateResult {
    RuntimeStatus status = RuntimeStatus::InternalError;
    std::string text;
    FinishReason finish_reason = FinishReason::Stop;
    TokenUsage usage;
    std::string error;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == RuntimeStatus::Ok;
    }
};

// Minimal boundary implemented by the currently proprietary inference
// runtime. JSON parsing, protocol compatibility, model admission, async worker
// ownership, and response encoding remain in the vendor framework.
class InferenceRuntime {
public:
    static std::unique_ptr<InferenceRuntime> Create();

    virtual ~InferenceRuntime() = default;

    InferenceRuntime(const InferenceRuntime &) = delete;
    InferenceRuntime &operator=(const InferenceRuntime &) = delete;

    // Synchronously loads the model and tokenizer. Success means Generate() can
    // be called immediately.
    virtual RuntimeResult Initialize(const RuntimeConfig &config) = 0;

    // Synchronous and blocking. The vendor guarantees that Generate() is never
    // called concurrently. The runtime manages physical KV-cache objects
    // internally, keyed by session_id, and may evict them at any time to remain
    // within kv_cache_budget_bytes.
    virtual GenerateResult Generate(const GenerateRequest &request) = 0;

    // May be called from a different thread while Generate() is running. It
    // causes Generate() to return with RuntimeStatus::Interrupted, waits for
    // inference to exit, releases all runtime resources, and then returns.
    // It is safe to call more than once.
    virtual void Shutdown() noexcept = 0;

protected:
    InferenceRuntime() = default;
};

}  // namespace lite_llm::design

#endif  // LITE_LLM_DESIGN_INFERENCE_RUNTIME_H
