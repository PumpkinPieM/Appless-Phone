#pragma once

#include <memory>
#include <string>

namespace lite_llm {
class LiteLlm {
public:
    static std::unique_ptr<LiteLlm> CreateFromConfig(const std::string& configPath);

    explicit LiteLlm(std::string modelText);
    ~LiteLlm() = default;

    LiteLlm(const LiteLlm&) = delete;
    LiteLlm& operator=(const LiteLlm&) = delete;

    std::string Generate(const std::string& prompt);

private:
    std::string modelText_;
};
} // namespace lite_llm
