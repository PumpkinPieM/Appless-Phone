#include "model.h"

#include "lite_llm.h"

#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace appless::lite_server {
namespace {

class LiteLlmModel final : public Model {
public:
    bool Build(const std::string &config_path, std::string *error) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            std::unique_ptr<lite_llm::LiteLlm> next = lite_llm::LiteLlm::CreateFromConfig(config_path);
            if (!next) {
                *error = "LiteLlm::CreateFromConfig returned null";
                return false;
            }
            model_ = std::move(next);
            return true;
        } catch (const std::exception &exception) {
            *error = exception.what();
            return false;
        } catch (...) {
            *error = "unknown model construction failure";
            return false;
        }
    }

    GenerationResult Generate(const std::string &prompt) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!model_) {
            return {false, "", "model is not built"};
        }
        try {
            std::string output = model_->Generate(prompt);
            if (output.empty()) {
                return {false, "", "vendor generation returned an empty response"};
            }
            return {true, std::move(output), ""};
        } catch (const std::exception &exception) {
            return {false, "", exception.what()};
        } catch (...) {
            return {false, "", "unknown vendor generation failure"};
        }
    }

private:
    std::mutex mutex_;
    std::unique_ptr<lite_llm::LiteLlm> model_;
};

}  // namespace

std::unique_ptr<Model> CreateModel()
{
    return std::make_unique<LiteLlmModel>();
}

}  // namespace appless::lite_server
