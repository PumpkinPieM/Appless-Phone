#pragma once

#include <memory>
#include <string>

namespace lite_llm {
class LiteLlm {
public:
    static std::unique_ptr<LiteLlm> CreateFromConfig(const std::string &configPath);

    LiteLlm(std::string apiKey, std::string model, std::string endpoint);
    ~LiteLlm() = default;

    LiteLlm(const LiteLlm &) = delete;
    LiteLlm &operator=(const LiteLlm &) = delete;

    std::string Generate(const std::string &prompt);

private:
    std::string apiKey_;
    std::string model_;
    std::string endpoint_;
};
} // namespace lite_llm
