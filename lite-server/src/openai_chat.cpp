#include "openai_chat.h"

#include "json_value.h"
#include "protocol_codec.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace appless::lite_server {
namespace {

bool IsRoleValid(const std::string &role)
{
    if (role.empty() || role.size() > 32) {
        return false;
    }
    for (const unsigned char current : role) {
        if (std::isalnum(current) == 0 && current != '_' && current != '-') {
            return false;
        }
    }
    return true;
}

bool ReadMessageContent(const JsonValue &value, std::string *content, std::string *error)
{
    if (value.type() == JsonValue::Type::String) {
        *content = value.string();
        return true;
    }
    if (value.type() != JsonValue::Type::Array) {
        *error = "message content must be a string or an array of text parts";
        return false;
    }
    std::string combined;
    for (const JsonValue &part : value.array()) {
        if (part.type() != JsonValue::Type::Object) {
            continue;
        }
        const JsonValue *type = part.Find("type");
        const JsonValue *text = part.Find("text");
        if (type == nullptr || type->type() != JsonValue::Type::String || type->string() != "text" ||
            text == nullptr || text->type() != JsonValue::Type::String) {
            continue;
        }
        if (!combined.empty()) {
            combined.push_back('\n');
        }
        combined += text->string();
    }
    if (combined.empty()) {
        *error = "message content contains no supported text parts";
        return false;
    }
    *content = std::move(combined);
    return true;
}

}  // namespace

bool ParseChatRequest(const std::string &body, ChatRequest *request, std::string *error)
{
    if (request == nullptr || error == nullptr) {
        return false;
    }
    JsonValue root;
    if (!ParseJson(body, &root, error)) {
        return false;
    }
    if (root.type() != JsonValue::Type::Object) {
        *error = "chat request must be a JSON object";
        return false;
    }
    const JsonValue *model = root.Find("model");
    request->model = model != nullptr && model->type() == JsonValue::Type::String && !model->string().empty()
        ? model->string() : "lite-local";
    const JsonValue *messages = root.Find("messages");
    if (messages == nullptr || messages->type() != JsonValue::Type::Array || messages->array().empty()) {
        *error = "chat request requires a non-empty messages array";
        return false;
    }
    if (messages->array().size() > 1024) {
        *error = "chat request contains too many messages";
        return false;
    }

    std::string prompt;
    for (const JsonValue &message : messages->array()) {
        if (message.type() != JsonValue::Type::Object) {
            *error = "each message must be a JSON object";
            return false;
        }
        const JsonValue *role_value = message.Find("role");
        const JsonValue *content_value = message.Find("content");
        if (role_value == nullptr || role_value->type() != JsonValue::Type::String ||
            !IsRoleValid(role_value->string())) {
            *error = "each message requires a valid role";
            return false;
        }
        if (content_value == nullptr) {
            *error = "each message requires content";
            return false;
        }
        std::string content;
        if (!ReadMessageContent(*content_value, &content, error)) {
            return false;
        }
        prompt += "[" + role_value->string() + "]\n" + content + "\n\n";
    }
    prompt += "[assistant]\n";
    request->prompt = std::move(prompt);
    return true;
}

std::string BuildChatCompletion(const std::string &request_id, const std::string &model,
                                const std::string &content)
{
    const auto created = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "{\"id\":" + protocol::EscapeJsonString("chatcmpl-lite-" + request_id) +
        ",\"object\":\"chat.completion\",\"created\":" + std::to_string(created) +
        ",\"model\":" + protocol::EscapeJsonString(model) +
        ",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":" +
        protocol::EscapeJsonString(content) + "},\"finish_reason\":\"stop\"}]}";
}

}  // namespace appless::lite_server
