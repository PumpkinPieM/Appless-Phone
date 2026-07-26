#include "protocol_codec.h"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <sstream>

namespace appless::lite_proxy::protocol {
namespace {

constexpr std::uint8_t kMagic[4] = {'L', 'T', 'S', '1'};

void SkipWhitespace(const std::string &text, std::size_t *cursor)
{
    while (*cursor < text.size() && std::isspace(static_cast<unsigned char>(text[*cursor])) != 0) {
        ++(*cursor);
    }
}

bool ScanJsonString(const std::string &text, std::size_t start, std::size_t *end)
{
    if (start >= text.size() || text[start] != '"') {
        return false;
    }
    bool escaped = false;
    for (std::size_t index = start + 1; index < text.size(); ++index) {
        const char current = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (current == '\\') {
            escaped = true;
            continue;
        }
        if (current == '"') {
            *end = index + 1;
            return true;
        }
    }
    return false;
}

bool AppendCodePoint(std::uint32_t code_point, std::string *output)
{
    if (code_point <= 0x7F) {
        output->push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        output->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        return false;
    }
    return true;
}

int HexValue(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool ReadHexQuad(const std::string &text, std::size_t start, std::uint32_t *value)
{
    if (start + 4 > text.size()) {
        return false;
    }
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const int digit = HexValue(text[start + index]);
        if (digit < 0) {
            return false;
        }
        result = (result << 4U) | static_cast<std::uint32_t>(digit);
    }
    *value = result;
    return true;
}

bool DecodeJsonString(const std::string &raw, std::string *value)
{
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        return false;
    }
    std::string decoded;
    decoded.reserve(raw.size() - 2);
    for (std::size_t index = 1; index + 1 < raw.size(); ++index) {
        const char current = raw[index];
        if (current != '\\') {
            if (static_cast<unsigned char>(current) < 0x20) {
                return false;
            }
            decoded.push_back(current);
            continue;
        }
        if (++index + 1 >= raw.size()) {
            return false;
        }
        const char escaped = raw[index];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                decoded.push_back(escaped);
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'u': {
                std::uint32_t code_point = 0;
                if (!ReadHexQuad(raw, index + 1, &code_point)) {
                    return false;
                }
                index += 4;
                if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                    if (index + 6 >= raw.size() || raw[index + 1] != '\\' || raw[index + 2] != 'u') {
                        return false;
                    }
                    std::uint32_t low = 0;
                    if (!ReadHexQuad(raw, index + 3, &low) || low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    code_point = 0x10000 + ((code_point - 0xD800) << 10U) + (low - 0xDC00);
                    index += 6;
                } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                    return false;
                }
                if (!AppendCodePoint(code_point, &decoded)) {
                    return false;
                }
                break;
            }
            default:
                return false;
        }
    }
    *value = std::move(decoded);
    return true;
}

bool ScanJsonValue(const std::string &text, std::size_t start, std::size_t *end)
{
    if (start >= text.size()) {
        return false;
    }
    if (text[start] == '"') {
        return ScanJsonString(text, start, end);
    }
    if (text[start] == '{' || text[start] == '[') {
        const char opening = text[start];
        const char closing = opening == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        for (std::size_t index = start; index < text.size(); ++index) {
            const char current = text[index];
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (current == '\\') {
                    escaped = true;
                } else if (current == '"') {
                    in_string = false;
                }
                continue;
            }
            if (current == '"') {
                in_string = true;
            } else if (current == opening) {
                ++depth;
            } else if (current == closing && --depth == 0) {
                *end = index + 1;
                return true;
            }
        }
        return false;
    }
    std::size_t cursor = start;
    while (cursor < text.size() && text[cursor] != ',' && text[cursor] != '}') {
        ++cursor;
    }
    while (cursor > start && std::isspace(static_cast<unsigned char>(text[cursor - 1])) != 0) {
        --cursor;
    }
    if (cursor == start) {
        return false;
    }
    *end = cursor;
    return true;
}

bool ReadRawField(const std::string &json, const std::string &key, std::string *raw_value)
{
    std::size_t cursor = 0;
    SkipWhitespace(json, &cursor);
    if (cursor >= json.size() || json[cursor++] != '{') {
        return false;
    }
    while (cursor < json.size()) {
        SkipWhitespace(json, &cursor);
        if (cursor < json.size() && json[cursor] == '}') {
            return false;
        }
        const std::size_t key_start = cursor;
        std::size_t key_end = 0;
        if (!ScanJsonString(json, key_start, &key_end)) {
            return false;
        }
        std::string decoded_key;
        if (!DecodeJsonString(json.substr(key_start, key_end - key_start), &decoded_key)) {
            return false;
        }
        cursor = key_end;
        SkipWhitespace(json, &cursor);
        if (cursor >= json.size() || json[cursor++] != ':') {
            return false;
        }
        SkipWhitespace(json, &cursor);
        const std::size_t value_start = cursor;
        std::size_t value_end = 0;
        if (!ScanJsonValue(json, value_start, &value_end)) {
            return false;
        }
        if (decoded_key == key) {
            *raw_value = json.substr(value_start, value_end - value_start);
            return true;
        }
        cursor = value_end;
        SkipWhitespace(json, &cursor);
        if (cursor < json.size() && json[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (cursor < json.size() && json[cursor] == '}') {
            return false;
        }
        return false;
    }
    return false;
}

}  // namespace

std::vector<std::uint8_t> EncodeFrame(const std::string &payload)
{
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame(kFrameHeaderBytes + payload.size());
    for (std::size_t index = 0; index < 4; ++index) {
        frame[index] = kMagic[index];
    }
    frame[4] = static_cast<std::uint8_t>((size >> 24U) & 0xFFU);
    frame[5] = static_cast<std::uint8_t>((size >> 16U) & 0xFFU);
    frame[6] = static_cast<std::uint8_t>((size >> 8U) & 0xFFU);
    frame[7] = static_cast<std::uint8_t>(size & 0xFFU);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame[kFrameHeaderBytes + index] = static_cast<std::uint8_t>(payload[index]);
    }
    return frame;
}

bool DecodeFrameHeader(const std::uint8_t *header, std::uint32_t *payload_size)
{
    if (header == nullptr || payload_size == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (header[index] != kMagic[index]) {
            return false;
        }
    }
    *payload_size = (static_cast<std::uint32_t>(header[4]) << 24U) |
        (static_cast<std::uint32_t>(header[5]) << 16U) |
        (static_cast<std::uint32_t>(header[6]) << 8U) |
        static_cast<std::uint32_t>(header[7]);
    return true;
}

std::string EscapeJsonString(const std::string &value)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char current : value) {
        switch (current) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (current < 0x20) {
                    constexpr char kHex[] = "0123456789abcdef";
                    output << "\\u00" << kHex[current >> 4U] << kHex[current & 0x0FU];
                } else {
                    output << static_cast<char>(current);
                }
        }
    }
    output << '"';
    return output.str();
}

bool ReadStringField(const std::string &json, const std::string &key, std::string *value)
{
    std::string raw;
    return value != nullptr && ReadRawField(json, key, &raw) && DecodeJsonString(raw, value);
}

bool ReadIntegerField(const std::string &json, const std::string &key, int *value)
{
    if (value == nullptr) {
        return false;
    }
    std::string raw;
    if (!ReadRawField(json, key, &raw) || raw.empty()) {
        return false;
    }
    char *end = nullptr;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

std::string BuildHelloAck()
{
    return "{\"type\":\"hello_ack\",\"protocol\":1}";
}

std::string BuildPong()
{
    return "{\"type\":\"pong\"}";
}

std::string BuildChatRequest(const std::string &request_id, const std::string &body)
{
    return "{\"type\":\"chat_request\",\"id\":" + EscapeJsonString(request_id) +
        ",\"body\":" + EscapeJsonString(body) + "}";
}

}  // namespace appless::lite_proxy::protocol
