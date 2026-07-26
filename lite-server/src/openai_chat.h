#ifndef APPLESS_LITE_SERVER_OPENAI_CHAT_H
#define APPLESS_LITE_SERVER_OPENAI_CHAT_H

#include <string>

namespace appless::lite_server {

struct ChatRequest {
    std::string model;
    std::string prompt;
};

bool ParseChatRequest(const std::string &body, ChatRequest *request, std::string *error);
std::string BuildChatCompletion(const std::string &request_id, const std::string &model,
                                const std::string &content);

}  // namespace appless::lite_server

#endif
