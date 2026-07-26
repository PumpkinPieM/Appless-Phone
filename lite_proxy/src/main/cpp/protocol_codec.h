#ifndef APPLESS_LITE_PROXY_PROTOCOL_CODEC_H
#define APPLESS_LITE_PROXY_PROTOCOL_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace appless::lite_proxy::protocol {

constexpr std::size_t kFrameHeaderBytes = 8;
constexpr std::uint32_t kProtocolVersion = 1;

std::vector<std::uint8_t> EncodeFrame(const std::string &payload);
bool DecodeFrameHeader(const std::uint8_t *header, std::uint32_t *payload_size);

std::string EscapeJsonString(const std::string &value);
bool ReadStringField(const std::string &json, const std::string &key, std::string *value);
bool ReadIntegerField(const std::string &json, const std::string &key, int *value);

std::string BuildHelloAck();
std::string BuildPong();
std::string BuildChatRequest(const std::string &request_id, const std::string &body);

}  // namespace appless::lite_proxy::protocol

#endif
