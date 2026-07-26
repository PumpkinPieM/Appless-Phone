#ifndef APPLESS_LITE_PROXY_GATEWAY_H
#define APPLESS_LITE_PROXY_GATEWAY_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace appless::lite_proxy {

struct ProxyOptions {
    std::uint16_t http_port = 0;
    std::uint16_t reverse_port = 0;
    std::string auth_token;
    std::uint32_t request_timeout_ms = 120000;
    std::size_t max_body_bytes = 4U * 1024U * 1024U;
};

struct ProxyStatus {
    std::string state = "stopped";
    bool running = false;
    bool server_connected = false;
    std::uint16_t http_port = 0;
    std::uint16_t reverse_port = 0;
    std::string base_url;
    std::string auth_token;
    std::size_t pending_requests = 0;
    std::string last_error;
};

class ProxyGateway final {
public:
    static ProxyGateway &Instance();

    bool Start(const ProxyOptions &options, std::string *error);
    void Stop();
    ProxyStatus Status() const;

    ProxyGateway(const ProxyGateway &) = delete;
    ProxyGateway &operator=(const ProxyGateway &) = delete;

private:
    class Impl;

    ProxyGateway();
    ~ProxyGateway();

    std::unique_ptr<Impl> impl_;
};

}  // namespace appless::lite_proxy

#endif
