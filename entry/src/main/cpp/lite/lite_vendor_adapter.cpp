#include "lite_vendor_adapter.h"

#include "lite_runtime.h"
#include "lite_llm.h"

#include <exception>
#include <memory>
#include <string>

namespace {
class LiteLlmVendorAdapter final : public LiteVendorAdapter {
public:
    void Initialize(const std::string& configPath) override {
        try {
            model_ = lite_llm::LiteLlm::CreateFromConfig(configPath);
            if (!model_) {
                throw LiteRuntimeError("MODEL_LOAD_FAILED", "Vendor model creation returned null.");
            }
        } catch (const LiteRuntimeError&) {
            throw;
        } catch (const std::exception& error) {
            throw LiteRuntimeError("MODEL_LOAD_FAILED", error.what());
        } catch (...) {
            throw LiteRuntimeError("MODEL_LOAD_FAILED", "Vendor model creation failed.");
        }
    }

    std::string Generate(const std::string& prompt) override {
        if (!model_) {
            throw LiteRuntimeError("MODEL_NOT_INITIALIZED", "The vendor model is not initialized.");
        }
        try {
            return model_->Generate(prompt);
        } catch (const LiteRuntimeError&) {
            throw;
        } catch (const std::exception& error) {
            throw LiteRuntimeError("GENERATION_FAILED", error.what());
        } catch (...) {
            throw LiteRuntimeError("GENERATION_FAILED", "Vendor generation failed.");
        }
    }

    void Release() override {
        try {
            model_.reset();
        } catch (const std::exception& error) {
            throw LiteRuntimeError("GENERATION_FAILED", error.what());
        } catch (...) {
            throw LiteRuntimeError("GENERATION_FAILED", "Vendor release failed.");
        }
    }

private:
    std::unique_ptr<lite_llm::LiteLlm> model_;
};
} // namespace

std::unique_ptr<LiteVendorAdapter> CreateLiteVendorAdapter() {
    return std::make_unique<LiteLlmVendorAdapter>();
}

bool IsLiteVendorAvailable() {
    return true;
}
