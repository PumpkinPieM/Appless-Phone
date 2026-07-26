#include "json_value.h"
#include "model.h"
#include "openai_chat.h"
#include "protocol_codec.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace lite = appless::lite_server;
namespace protocol = appless::lite_server::protocol;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void TestJson()
{
    lite::JsonValue value;
    std::string error;
    Expect(lite::ParseJson("{\"text\":\"line\\n\\u4f60\\u597d\",\"number\":1.5}", &value, &error),
        "parse JSON object");
    const lite::JsonValue *text = value.Find("text");
    Expect(text != nullptr && text->type() == lite::JsonValue::Type::String && text->string() == "line\n你好",
        "decode JSON string");
    Expect(!lite::ParseJson("{\"text\":\"\\udc00\"}", &value, &error), "reject unpaired surrogate");
    Expect(!lite::ParseJson("[1,]", &value, &error), "reject trailing comma");
}

void TestProtocol()
{
    const std::string payload = protocol::BuildHello("secret-token");
    const std::vector<std::uint8_t> frame = protocol::EncodeFrame(payload);
    std::uint32_t size = 0;
    Expect(frame.size() == payload.size() + protocol::kFrameHeaderBytes, "frame size");
    Expect(protocol::DecodeFrameHeader(frame.data(), &size) && size == payload.size(), "frame header");

    lite::JsonValue hello;
    std::string error;
    Expect(lite::ParseJson(payload, &hello, &error), "hello is valid JSON");
    const lite::JsonValue *token = hello.Find("auth_token");
    Expect(token != nullptr && token->type() == lite::JsonValue::Type::String && token->string() == "secret-token",
        "hello token");
}

void TestOpenAiChat()
{
    const std::string body =
        "{\"model\":\"mock-model\",\"stream\":true,\"messages\":["
        "{\"role\":\"system\",\"content\":\"Follow tools.\"},"
        "{\"role\":\"user\",\"content\":\"Hello\"}]}";
    lite::ChatRequest request;
    std::string error;
    Expect(lite::ParseChatRequest(body, &request, &error), "parse OpenAI chat request");
    Expect(request.model == "mock-model", "model name");
    Expect(request.prompt == "[system]\nFollow tools.\n\n[user]\nHello\n\n[assistant]\n", "role prompt");

    const std::string completion = lite::BuildChatCompletion("7", request.model, "Final: mock");
    lite::JsonValue response;
    Expect(lite::ParseJson(completion, &response, &error), "completion is valid JSON");
    const lite::JsonValue *id = response.Find("id");
    Expect(id != nullptr && id->type() == lite::JsonValue::Type::String && id->string() == "chatcmpl-lite-7",
        "completion id");

    Expect(!lite::ParseChatRequest("{\"messages\":[]}", &request, &error), "reject empty messages");
    Expect(!lite::ParseChatRequest("{\"messages\":[{\"role\":\"user\"}]}", &request, &error),
        "reject missing content");
}

void TestDeepSeekVendor(const std::string &config_path)
{
    if (config_path.empty()) {
        return;
    }
    std::unique_ptr<lite::Model> model = lite::CreateModel();
    std::string error;
    Expect(model->Build(config_path, &error), "build DeepSeek vendor: " + error);
    const lite::GenerationResult result = model->Generate("Reply with exactly: lite-server-ok");
    Expect(result.ok, "generate through DeepSeek vendor: " + result.error);
    Expect(!result.text.empty(), "DeepSeek response content");
}

}  // namespace

int main(int argc, char **argv)
{
    TestJson();
    TestProtocol();
    TestOpenAiChat();
    TestDeepSeekVendor(argc > 1 ? argv[1] : "");
    if (failures != 0) {
        return 1;
    }
    std::cout << "lite-server-tests: PASS\n";
    return 0;
}
