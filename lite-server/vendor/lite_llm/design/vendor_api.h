#ifndef LITE_LLM_DESIGN_VENDOR_API_H
#define LITE_LLM_DESIGN_VENDOR_API_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lite_llm::design {

// A complete transport-neutral response. lite-server should forward these
// fields without interpreting model output or constructing completion JSON.
struct VendorResponse {
    int status_code = 500;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

struct InitializeResult {
    bool ok = false;
    std::string error;
};

enum class SubmitStatus {
    Accepted,
    Busy,
    InvalidRequest,
    NotReady,
    ShuttingDown,
    InternalError,
};

struct SubmitResult {
    SubmitStatus status = SubmitStatus::InternalError;

    // Set when the request was rejected synchronously. The server forwards the
    // response and must not expect the completion callback to run.
    std::optional<VendorResponse> immediate_response;

    [[nodiscard]] bool accepted() const noexcept
    {
        return status == SubmitStatus::Accepted;
    }
};

// Called exactly once for every accepted generation. It may run on a
// vendor-owned worker thread. It is never called for a rejected submission or
// after Shutdown() has returned.
using CompletionCallback = std::function<void(VendorResponse response)>;

class LiteLlmVendor {
public:
    static LiteLlmVendor &Instance();

    LiteLlmVendor(const LiteLlmVendor &) = delete;
    LiteLlmVendor &operator=(const LiteLlmVendor &) = delete;

    // Synchronously validates configuration and eagerly loads the initial
    // model. Success means the vendor is ready to accept generation.
    virtual InitializeResult Initialize(std::string_view config_json) = 0;

    // Non-blocking. Only one generation may be active. A second generation is
    // rejected with SubmitStatus::Busy and is never queued.
    //
    // request_json is expected to use the OpenAI chat-completions shape with a
    // namespaced lite_llm extension containing protocol_version, session_id,
    // and cache policy. There is no vendor-level request ID.
    virtual SubmitResult GenerateAsync(
        std::string request_json,
        CompletionCallback on_complete) = 0;

    // Stops admission, interrupts active inference through the runtime,
    // joins the worker, releases session caches and model resources, and then
    // returns. It is safe to call more than once.
    virtual void Shutdown() noexcept = 0;

protected:
    LiteLlmVendor() = default;
    virtual ~LiteLlmVendor() = default;
};

}  // namespace lite_llm::design

#endif  // LITE_LLM_DESIGN_VENDOR_API_H
