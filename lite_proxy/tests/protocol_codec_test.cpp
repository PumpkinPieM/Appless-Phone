#include "protocol_codec.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace protocol = appless::lite_proxy::protocol;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    const std::string payload = "{\"type\":\"ping\"}";
    const std::vector<std::uint8_t> frame = protocol::EncodeFrame(payload);
    Expect(frame.size() == payload.size() + protocol::kFrameHeaderBytes, "encoded frame size");
    std::uint32_t payload_size = 0;
    Expect(protocol::DecodeFrameHeader(frame.data(), &payload_size), "valid frame header");
    Expect(payload_size == payload.size(), "decoded payload size");

    std::string value;
    Expect(protocol::ReadStringField(
        "{\"ignored\":{\"nested\":true},\"text\":\"line\\n\\u4f60\\u597d\"}", "text", &value),
        "read escaped string field");
    Expect(value == "line\n你好", "decode escaped UTF-8");

    int number = 0;
    Expect(protocol::ReadIntegerField("{\"protocol\":1}", "protocol", &number), "read integer field");
    Expect(number == 1, "integer field value");

    const std::string body = "{\"model\":\"mock\",\"messages\":[{\"role\":\"user\",\"content\":\"a \\\"quote\\\"\"}]}";
    const std::string request = protocol::BuildChatRequest("42", body);
    Expect(protocol::ReadStringField(request, "type", &value) && value == "chat_request", "request type");
    Expect(protocol::ReadStringField(request, "id", &value) && value == "42", "request id");
    Expect(protocol::ReadStringField(request, "body", &value) && value == body, "request body round trip");
    Expect(!protocol::ReadStringField("{\"type\":false}", "type", &value), "reject non-string field");
    Expect(!protocol::ReadStringField("{\"text\":\"\\udc00\"}", "text", &value),
        "reject an unpaired low surrogate");
    Expect(!protocol::ReadStringField(std::string("{\"text\":\"bad") + '\n' + "value\"}", "text", &value),
        "reject an unescaped control character");

    if (failures != 0) {
        return 1;
    }
    std::cout << "protocol_codec_test: PASS\n";
    return 0;
}
