#include "lite_llm.h"

#include <hilog/log.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr unsigned int MOCK_LOG_DOMAIN = 0x0000;
constexpr const char* MOCK_LOG_TAG = "LiteLlmMock";

std::string ReadFile(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open mock model file: " + path);
    }
    std::ostringstream content;
    content << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("Unable to read mock model file: " + path);
    }
    return content.str();
}

std::string DirectoryName(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

std::string JoinPath(const std::string& directory, const std::string& relativePath) {
    if (directory.empty()) return relativePath;
    if (relativePath.empty()) return directory;
    return directory + "/" + relativePath;
}

std::string JsonStringValue(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    const std::size_t keyStart = json.find(token);
    if (keyStart == std::string::npos) {
        throw std::runtime_error("Mock config is missing string entry: " + key);
    }
    std::size_t cursor = json.find(':', keyStart + token.size());
    if (cursor == std::string::npos) {
        throw std::runtime_error("Mock config entry has no value: " + key);
    }
    ++cursor;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor] != '"') {
        throw std::runtime_error("Mock config entry must be a string: " + key);
    }
    ++cursor;
    std::string value;
    bool escaped = false;
    for (; cursor < json.size(); ++cursor) {
        const char current = json[cursor];
        if (escaped) {
            value.push_back(current);
            escaped = false;
        } else if (current == '\\') {
            escaped = true;
        } else if (current == '"') {
            return value;
        } else {
            value.push_back(current);
        }
    }
    throw std::runtime_error("Mock config entry is not terminated: " + key);
}
} // namespace

namespace lite_llm {
std::unique_ptr<LiteLlm> LiteLlm::CreateFromConfig(const std::string& configPath) {
    const std::string config = ReadFile(configPath);
    const std::string fooPath = JoinPath(DirectoryName(configPath), JsonStringValue(config, "foo"));
    const std::string fooContent = ReadFile(fooPath);
    OH_LOG_Print(LOG_APP, LOG_INFO, MOCK_LOG_DOMAIN, MOCK_LOG_TAG,
        "Mock model file loaded path=%{public}s content=%{public}s",
        fooPath.c_str(), fooContent.c_str());
    return std::make_unique<LiteLlm>(fooContent);
}

LiteLlm::LiteLlm(std::string modelText) : modelText_(std::move(modelText)) {}

std::string LiteLlm::Generate(const std::string& prompt) {
    (void)prompt;
    OH_LOG_Print(LOG_APP, LOG_INFO, MOCK_LOG_DOMAIN, MOCK_LOG_TAG,
        "Generate returning foo content bytes=%{public}zu content=%{public}s",
        modelText_.size(), modelText_.c_str());
    return modelText_;
}
} // namespace lite_llm
