#include "proxy_gateway.h"

#include "protocol_codec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <poll.h>
#include <random>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unistd.h>

namespace appless::lite_proxy {
namespace {

constexpr std::size_t kMaxHttpHeaderBytes = 64U * 1024U;
constexpr std::size_t kMaxConcurrentHttpClients = 16;
constexpr std::size_t kProtocolOverheadBytes = 128U * 1024U;
constexpr int kSocketPollIntervalMs = 500;
constexpr int kHandshakeTimeoutMs = 10000;
constexpr int kHttpHeaderTimeoutMs = 10000;
constexpr int kHttpBodyTimeoutMs = 30000;
constexpr int kListenBacklog = 16;

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct PendingRequest {
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    int status = 502;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::string error;
};

std::string ErrnoMessage(const std::string &operation)
{
    return operation + " failed: " + std::strerror(errno) + " (errno=" + std::to_string(errno) + ")";
}

void ShutdownAndClose(int socket_fd)
{
    if (socket_fd < 0) {
        return;
    }
    (void)shutdown(socket_fd, SHUT_RDWR);
    (void)close(socket_fd);
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char current) {
        if (current >= 'A' && current <= 'Z') {
            return static_cast<char>(current - 'A' + 'a');
        }
        return static_cast<char>(current);
    });
    return value;
}

std::string Trim(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

bool ConstantTimeEquals(const std::string &left, const std::string &right)
{
    const std::size_t compare_size = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < compare_size; ++index) {
        const std::uint8_t left_value = index < left.size() ? static_cast<std::uint8_t>(left[index]) : 0;
        const std::uint8_t right_value = index < right.size() ? static_cast<std::uint8_t>(right[index]) : 0;
        difference |= static_cast<std::size_t>(left_value ^ right_value);
    }
    return difference == 0;
}

std::string GenerateAuthToken()
{
    std::array<std::uint8_t, 24> random_bytes{};
    bool generated = false;
    const int random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random_fd >= 0) {
        std::size_t offset = 0;
        while (offset < random_bytes.size()) {
            const ssize_t count = read(random_fd, random_bytes.data() + offset, random_bytes.size() - offset);
            if (count > 0) {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        generated = offset == random_bytes.size();
        (void)close(random_fd);
    }
    if (!generated) {
        std::random_device source;
        for (std::uint8_t &value : random_bytes) {
            value = static_cast<std::uint8_t>(source());
        }
    }
    constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(random_bytes.size() * 2);
    for (const std::uint8_t value : random_bytes) {
        token.push_back(kHex[value >> 4U]);
        token.push_back(kHex[value & 0x0FU]);
    }
    return token;
}

int CreateLoopbackListener(std::uint16_t requested_port, std::uint16_t *actual_port, std::string *error)
{
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        *error = ErrnoMessage("socket");
        return -1;
    }
    int reuse = 1;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(requested_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        *error = ErrnoMessage("bind(127.0.0.1:" + std::to_string(requested_port) + ")");
        ShutdownAndClose(socket_fd);
        return -1;
    }
    if (listen(socket_fd, kListenBacklog) != 0) {
        *error = ErrnoMessage("listen");
        ShutdownAndClose(socket_fd);
        return -1;
    }
    sockaddr_in bound_address{};
    socklen_t bound_size = sizeof(bound_address);
    if (getsockname(socket_fd, reinterpret_cast<sockaddr *>(&bound_address), &bound_size) != 0) {
        *error = ErrnoMessage("getsockname");
        ShutdownAndClose(socket_fd);
        return -1;
    }
    *actual_port = ntohs(bound_address.sin_port);
    return socket_fd;
}

bool PollSocket(int socket_fd, short events, int timeout_ms, std::string *error)
{
    pollfd descriptor{};
    descriptor.fd = socket_fd;
    descriptor.events = events;
    while (true) {
        const int result = poll(&descriptor, 1, timeout_ms);
        if (result > 0) {
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                *error = "socket disconnected";
                return false;
            }
            return (descriptor.revents & events) != 0;
        }
        if (result == 0) {
            return false;
        }
        if (errno != EINTR) {
            *error = ErrnoMessage("poll");
            return false;
        }
    }
}

bool ReceiveExact(int socket_fd, std::uint8_t *buffer, std::size_t size, const std::atomic<bool> &running,
                  int total_timeout_ms, std::string *error)
{
    const auto started = std::chrono::steady_clock::now();
    std::size_t offset = 0;
    while (offset < size && running.load()) {
        if (total_timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= total_timeout_ms) {
                *error = "receive timed out";
                return false;
            }
        }
        std::string poll_error;
        if (!PollSocket(socket_fd, POLLIN, kSocketPollIntervalMs, &poll_error)) {
            if (!poll_error.empty()) {
                *error = poll_error;
                return false;
            }
            continue;
        }
        const ssize_t received = recv(socket_fd, buffer + offset, size - offset, 0);
        if (received > 0) {
            offset += static_cast<std::size_t>(received);
        } else if (received == 0) {
            *error = "peer closed the connection";
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            *error = ErrnoMessage("recv");
            return false;
        }
    }
    if (!running.load()) {
        *error = "proxy is stopping";
        return false;
    }
    return offset == size;
}

bool SendAll(int socket_fd, const std::uint8_t *data, std::size_t size, std::string *error)
{
    std::size_t offset = 0;
    while (offset < size) {
#ifdef MSG_NOSIGNAL
        const ssize_t sent = send(socket_fd, data + offset, size - offset, MSG_NOSIGNAL);
#else
        const ssize_t sent = send(socket_fd, data + offset, size - offset, 0);
#endif
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
        } else if (sent < 0 && errno == EINTR) {
            continue;
        } else {
            *error = ErrnoMessage("send");
            return false;
        }
    }
    return true;
}

bool SendFrameToSocket(int socket_fd, const std::string &payload, std::string *error)
{
    const std::vector<std::uint8_t> frame = protocol::EncodeFrame(payload);
    return SendAll(socket_fd, frame.data(), frame.size(), error);
}

bool ReceiveFrame(int socket_fd, std::size_t max_payload_bytes, const std::atomic<bool> &running,
                  int timeout_ms, std::string *payload, std::string *error)
{
    std::array<std::uint8_t, protocol::kFrameHeaderBytes> header{};
    if (!ReceiveExact(socket_fd, header.data(), header.size(), running, timeout_ms, error)) {
        return false;
    }
    std::uint32_t payload_size = 0;
    if (!protocol::DecodeFrameHeader(header.data(), &payload_size)) {
        *error = "invalid reverse protocol frame magic";
        return false;
    }
    if (payload_size == 0 || payload_size > max_payload_bytes) {
        *error = "invalid reverse protocol frame size: " + std::to_string(payload_size);
        return false;
    }
    std::vector<std::uint8_t> bytes(payload_size);
    if (!ReceiveExact(socket_fd, bytes.data(), bytes.size(), running, timeout_ms, error)) {
        return false;
    }
    payload->assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
}

bool ParseSize(const std::string &value, std::size_t *result)
{
    if (value.empty()) {
        return false;
    }
    std::size_t parsed = 0;
    for (const char current : value) {
        if (current < '0' || current > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(current - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    *result = parsed;
    return true;
}

bool ReadHttpRequest(int socket_fd, std::size_t max_body_bytes, const std::atomic<bool> &running,
                     HttpRequest *request, std::string *error)
{
    const auto header_started = std::chrono::steady_clock::now();
    std::string received;
    received.reserve(4096);
    std::size_t header_end = std::string::npos;
    while (running.load() && header_end == std::string::npos) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - header_started).count();
        if (elapsed >= kHttpHeaderTimeoutMs) {
            *error = "HTTP header receive timed out";
            return false;
        }
        if (received.size() >= kMaxHttpHeaderBytes) {
            *error = "HTTP headers exceed 64 KiB";
            return false;
        }
        std::string poll_error;
        if (!PollSocket(socket_fd, POLLIN, kSocketPollIntervalMs, &poll_error)) {
            if (!poll_error.empty()) {
                *error = poll_error;
                return false;
            }
            continue;
        }
        std::array<char, 4096> buffer{};
        const ssize_t count = recv(socket_fd, buffer.data(), buffer.size(), 0);
        if (count > 0) {
            received.append(buffer.data(), static_cast<std::size_t>(count));
            header_end = received.find("\r\n\r\n");
        } else if (count == 0) {
            *error = "HTTP client closed before sending headers";
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            *error = ErrnoMessage("recv HTTP headers");
            return false;
        }
    }
    if (header_end == std::string::npos) {
        *error = "proxy is stopping";
        return false;
    }
    const std::string headers_text = received.substr(0, header_end);
    const std::size_t first_line_end = headers_text.find("\r\n");
    const std::string request_line = first_line_end == std::string::npos ? headers_text : headers_text.substr(0, first_line_end);
    const std::size_t method_end = request_line.find(' ');
    const std::size_t path_end = method_end == std::string::npos ? std::string::npos : request_line.find(' ', method_end + 1);
    if (method_end == std::string::npos || path_end == std::string::npos ||
        request_line.substr(path_end + 1).rfind("HTTP/1.", 0) != 0) {
        *error = "malformed HTTP request line";
        return false;
    }
    request->method = request_line.substr(0, method_end);
    request->path = request_line.substr(method_end + 1, path_end - method_end - 1);

    std::size_t line_start = first_line_end == std::string::npos ? headers_text.size() : first_line_end + 2;
    while (line_start < headers_text.size()) {
        const std::size_t line_end = headers_text.find("\r\n", line_start);
        const std::string line = headers_text.substr(line_start,
            (line_end == std::string::npos ? headers_text.size() : line_end) - line_start);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            *error = "malformed HTTP header";
            return false;
        }
        request->headers[ToLower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 2;
    }
    if (request->headers.find("transfer-encoding") != request->headers.end()) {
        *error = "chunked HTTP request bodies are not supported";
        return false;
    }
    std::size_t content_length = 0;
    const auto length_it = request->headers.find("content-length");
    if (length_it != request->headers.end() && !ParseSize(length_it->second, &content_length)) {
        *error = "invalid Content-Length";
        return false;
    }
    if (content_length > max_body_bytes) {
        *error = "HTTP request body exceeds configured limit";
        return false;
    }
    const std::size_t body_start = header_end + 4;
    if (received.size() > body_start) {
        request->body = received.substr(body_start, std::min(content_length, received.size() - body_start));
    }
    const auto body_started = std::chrono::steady_clock::now();
    while (running.load() && request->body.size() < content_length) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - body_started).count();
        if (elapsed >= kHttpBodyTimeoutMs) {
            *error = "HTTP body receive timed out";
            return false;
        }
        std::string poll_error;
        if (!PollSocket(socket_fd, POLLIN, kSocketPollIntervalMs, &poll_error)) {
            if (!poll_error.empty()) {
                *error = poll_error;
                return false;
            }
            continue;
        }
        std::array<char, 8192> buffer{};
        const std::size_t wanted = std::min(buffer.size(), content_length - request->body.size());
        const ssize_t count = recv(socket_fd, buffer.data(), wanted, 0);
        if (count > 0) {
            request->body.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count == 0) {
            *error = "HTTP client closed before sending the complete body";
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            *error = ErrnoMessage("recv HTTP body");
            return false;
        }
    }
    return request->body.size() == content_length;
}

std::string HttpReason(int status)
{
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "Response";
    }
}

std::string SafeContentType(const std::string &content_type)
{
    const std::string lower = ToLower(content_type);
    if (lower.rfind("application/json", 0) == 0 || lower.rfind("text/event-stream", 0) == 0 ||
        lower.rfind("text/plain", 0) == 0) {
        return content_type;
    }
    return "application/json; charset=utf-8";
}

bool SendHttpResponse(int socket_fd, int status, const std::string &content_type, const std::string &body)
{
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << HttpReason(status) << "\r\n"
            << "Content-Type: " << SafeContentType(content_type) << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n\r\n";
    const std::string header_text = headers.str();
    std::string error;
    return SendAll(socket_fd, reinterpret_cast<const std::uint8_t *>(header_text.data()), header_text.size(), &error) &&
        SendAll(socket_fd, reinterpret_cast<const std::uint8_t *>(body.data()), body.size(), &error);
}

std::string ErrorBody(const std::string &message)
{
    return "{\"error\":{\"message\":" + protocol::EscapeJsonString(message) +
        ",\"type\":\"lite_proxy_error\"}}";
}

std::string PathWithoutQuery(const std::string &path)
{
    const std::size_t query = path.find('?');
    return query == std::string::npos ? path : path.substr(0, query);
}

}  // namespace

class ProxyGateway::Impl final {
public:
    ~Impl()
    {
        Stop();
    }

    bool Start(const ProxyOptions &requested_options, std::string *error)
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (running_.load()) {
            return true;
        }
        if (requested_options.auth_token.size() > 512) {
            *error = "authToken must not exceed 512 bytes";
            return false;
        }
        if (requested_options.request_timeout_ms < 1000 || requested_options.request_timeout_ms > 30U * 60U * 1000U) {
            *error = "requestTimeoutMs must be between 1000 and 1800000";
            return false;
        }
        if (requested_options.max_body_bytes < 1024 || requested_options.max_body_bytes > 64U * 1024U * 1024U) {
            *error = "maxBodyBytes must be between 1024 and 67108864";
            return false;
        }
        options_ = requested_options;
        if (options_.auth_token.empty()) {
            options_.auth_token = GenerateAuthToken();
        }

        std::uint16_t actual_http_port = 0;
        std::uint16_t actual_reverse_port = 0;
        std::string listen_error;
        http_listener_fd_ = CreateLoopbackListener(options_.http_port, &actual_http_port, &listen_error);
        if (http_listener_fd_ < 0) {
            SetLastError(listen_error);
            *error = listen_error;
            return false;
        }
        reverse_listener_fd_ = CreateLoopbackListener(options_.reverse_port, &actual_reverse_port, &listen_error);
        if (reverse_listener_fd_ < 0) {
            ShutdownAndClose(http_listener_fd_);
            http_listener_fd_ = -1;
            SetLastError(listen_error);
            *error = listen_error;
            return false;
        }
        options_.http_port = actual_http_port;
        options_.reverse_port = actual_reverse_port;
        next_request_id_.store(1);
        active_http_count_.store(0);
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            last_error_.clear();
            server_connected_ = false;
        }
        running_.store(true);
        http_accept_thread_ = std::thread(&Impl::HttpAcceptLoop, this);
        reverse_thread_ = std::thread(&Impl::ReverseAcceptLoop, this);
        return true;
    }

    void Stop()
    {
        std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (!running_.exchange(false)) {
            return;
        }
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            server_connected_ = false;
        }
        ShutdownAndClose(http_listener_fd_);
        http_listener_fd_ = -1;
        ShutdownAndClose(reverse_listener_fd_);
        reverse_listener_fd_ = -1;
        {
            std::lock_guard<std::mutex> reverse_lock(reverse_mutex_);
            ShutdownAndClose(reverse_fd_);
            reverse_fd_ = -1;
        }
        {
            std::lock_guard<std::mutex> clients_lock(http_clients_mutex_);
            for (const int client_fd : http_client_fds_) {
                (void)shutdown(client_fd, SHUT_RDWR);
            }
        }
        FailAllPending("proxy stopped");
        lifecycle_lock.unlock();

        if (http_accept_thread_.joinable()) {
            http_accept_thread_.join();
        }
        if (reverse_thread_.joinable()) {
            reverse_thread_.join();
        }
        for (std::thread &worker : http_workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        http_workers_.clear();
        std::lock_guard<std::mutex> final_lock(lifecycle_mutex_);
        options_.http_port = 0;
        options_.reverse_port = 0;
        options_.auth_token.clear();
    }

    ProxyStatus Status() const
    {
        ProxyStatus result;
        result.running = running_.load();
        result.http_port = options_.http_port;
        result.reverse_port = options_.reverse_port;
        result.auth_token = options_.auth_token;
        if (result.http_port != 0) {
            result.base_url = "http://127.0.0.1:" + std::to_string(result.http_port) + "/v1";
        }
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            result.server_connected = server_connected_;
            result.last_error = last_error_;
        }
        {
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            result.pending_requests = pending_.size();
        }
        if (!result.running) {
            result.state = "stopped";
        } else if (result.server_connected) {
            result.state = "ready";
        } else {
            result.state = "waiting";
        }
        return result;
    }

private:
    void SetLastError(const std::string &message)
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        last_error_ = message;
    }

    void HttpAcceptLoop()
    {
        while (running_.load()) {
            const int client_fd = accept(http_listener_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (!running_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                SetLastError(ErrnoMessage("accept HTTP client"));
                continue;
            }
            if (active_http_count_.load() >= kMaxConcurrentHttpClients) {
                (void)SendHttpResponse(client_fd, 429, "application/json; charset=utf-8",
                    ErrorBody("too many concurrent proxy clients"));
                ShutdownAndClose(client_fd);
                continue;
            }
            active_http_count_.fetch_add(1);
            {
                std::lock_guard<std::mutex> clients_lock(http_clients_mutex_);
                http_client_fds_.insert(client_fd);
            }
            http_workers_.emplace_back(&Impl::HandleHttpClient, this, client_fd);
        }
    }

    void HandleHttpClient(int client_fd)
    {
        HttpRequest request;
        std::string read_error;
        if (!ReadHttpRequest(client_fd, options_.max_body_bytes, running_, &request, &read_error)) {
            const int status = read_error.find("exceeds") != std::string::npos ? 413 : 400;
            (void)SendHttpResponse(client_fd, status, "application/json; charset=utf-8", ErrorBody(read_error));
            FinishHttpClient(client_fd);
            return;
        }
        const std::string path = PathWithoutQuery(request.path);
        if (request.method == "GET" && path == "/health") {
            (void)SendHttpResponse(client_fd, 200, "application/json; charset=utf-8",
                "{\"status\":\"ok\"}");
            FinishHttpClient(client_fd);
            return;
        }
        if (request.method == "GET" && path == "/ready") {
            const bool ready = Status().server_connected;
            (void)SendHttpResponse(client_fd, ready ? 200 : 503, "application/json; charset=utf-8",
                ready ? "{\"status\":\"ready\"}" : "{\"status\":\"waiting_for_server\"}");
            FinishHttpClient(client_fd);
            return;
        }
        if (path != "/v1/chat/completions" && path != "/chat/completions") {
            (void)SendHttpResponse(client_fd, 404, "application/json; charset=utf-8", ErrorBody("unknown proxy endpoint"));
            FinishHttpClient(client_fd);
            return;
        }
        if (request.method != "POST") {
            (void)SendHttpResponse(client_fd, 405, "application/json; charset=utf-8", ErrorBody("POST is required"));
            FinishHttpClient(client_fd);
            return;
        }
        const auto auth_it = request.headers.find("authorization");
        const std::string expected_auth = "Bearer " + options_.auth_token;
        if (auth_it == request.headers.end() || !ConstantTimeEquals(auth_it->second, expected_auth)) {
            (void)SendHttpResponse(client_fd, 401, "application/json; charset=utf-8", ErrorBody("invalid proxy bearer token"));
            FinishHttpClient(client_fd);
            return;
        }
        if (request.body.empty()) {
            (void)SendHttpResponse(client_fd, 400, "application/json; charset=utf-8", ErrorBody("request body is empty"));
            FinishHttpClient(client_fd);
            return;
        }
        if (!Status().server_connected) {
            (void)SendHttpResponse(client_fd, 503, "application/json; charset=utf-8",
                ErrorBody("local model server is not connected"));
            FinishHttpClient(client_fd);
            return;
        }

        const std::string request_id = std::to_string(next_request_id_.fetch_add(1));
        const auto pending = std::make_shared<PendingRequest>();
        {
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            pending_[request_id] = pending;
        }
        std::string send_error;
        if (!SendToServer(protocol::BuildChatRequest(request_id, request.body), &send_error)) {
            RemovePending(request_id, pending);
            (void)SendHttpResponse(client_fd, 502, "application/json; charset=utf-8", ErrorBody(send_error));
            FinishHttpClient(client_fd);
            return;
        }

        std::unique_lock<std::mutex> pending_lock(pending->mutex);
        const bool completed = pending->condition.wait_for(pending_lock,
            std::chrono::milliseconds(options_.request_timeout_ms), [&pending] { return pending->done; });
        if (!completed) {
            pending_lock.unlock();
            RemovePending(request_id, pending);
            (void)SendHttpResponse(client_fd, 504, "application/json; charset=utf-8",
                ErrorBody("local model request timed out"));
            FinishHttpClient(client_fd);
            return;
        }
        const int response_status = pending->status;
        const std::string response_type = pending->content_type;
        const std::string response_body = pending->error.empty() ? pending->body : ErrorBody(pending->error);
        pending_lock.unlock();
        (void)SendHttpResponse(client_fd, response_status, response_type, response_body);
        FinishHttpClient(client_fd);
    }

    void FinishHttpClient(int client_fd)
    {
        {
            std::lock_guard<std::mutex> clients_lock(http_clients_mutex_);
            http_client_fds_.erase(client_fd);
        }
        ShutdownAndClose(client_fd);
        active_http_count_.fetch_sub(1);
    }

    void ReverseAcceptLoop()
    {
        while (running_.load()) {
            const int client_fd = accept(reverse_listener_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (!running_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                SetLastError(ErrnoMessage("accept reverse server"));
                continue;
            }
            int keepalive = 1;
            (void)setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            RunReverseSession(client_fd);
        }
    }

    void RunReverseSession(int client_fd)
    {
        const std::size_t max_frame_bytes = options_.max_body_bytes + kProtocolOverheadBytes;
        std::string hello;
        std::string session_error;
        if (!ReceiveFrame(client_fd, max_frame_bytes, running_, kHandshakeTimeoutMs, &hello, &session_error)) {
            SetLastError("reverse handshake failed: " + session_error);
            ShutdownAndClose(client_fd);
            return;
        }
        std::string type;
        std::string token;
        int version = 0;
        if (!protocol::ReadStringField(hello, "type", &type) || type != "hello" ||
            !protocol::ReadIntegerField(hello, "protocol", &version) ||
            version != static_cast<int>(protocol::kProtocolVersion) ||
            !protocol::ReadStringField(hello, "auth_token", &token) ||
            !ConstantTimeEquals(token, options_.auth_token)) {
            SetLastError("reverse handshake rejected");
            ShutdownAndClose(client_fd);
            return;
        }
        if (!SendFrameToSocket(client_fd, protocol::BuildHelloAck(), &session_error)) {
            SetLastError("reverse handshake acknowledgement failed: " + session_error);
            ShutdownAndClose(client_fd);
            return;
        }
        {
            std::lock_guard<std::mutex> reverse_lock(reverse_mutex_);
            reverse_fd_ = client_fd;
        }
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            server_connected_ = true;
            last_error_.clear();
        }
        while (running_.load()) {
            std::string payload;
            if (!ReceiveFrame(client_fd, max_frame_bytes, running_, 0, &payload, &session_error)) {
                break;
            }
            if (!HandleServerMessage(payload, &session_error)) {
                break;
            }
        }
        {
            std::lock_guard<std::mutex> reverse_lock(reverse_mutex_);
            if (reverse_fd_ == client_fd) {
                ShutdownAndClose(reverse_fd_);
                reverse_fd_ = -1;
            }
        }
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            server_connected_ = false;
            if (running_.load() && !session_error.empty()) {
                last_error_ = "reverse server disconnected: " + session_error;
            }
        }
        FailAllPending(session_error.empty() ? "local model server disconnected" : session_error);
    }

    bool HandleServerMessage(const std::string &payload, std::string *error)
    {
        std::string type;
        if (!protocol::ReadStringField(payload, "type", &type)) {
            *error = "server message has no valid type";
            return false;
        }
        if (type == "ping") {
            return SendToServer(protocol::BuildPong(), error);
        }
        if (type == "status" || type == "pong") {
            return true;
        }
        if (type != "chat_response" && type != "error") {
            *error = "unsupported server message type: " + type;
            return false;
        }
        std::string request_id;
        if (!protocol::ReadStringField(payload, "id", &request_id)) {
            *error = "server response has no request id";
            return false;
        }
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            const auto found = pending_.find(request_id);
            if (found == pending_.end()) {
                return true;
            }
            pending = found->second;
            pending_.erase(found);
        }
        int status = type == "error" ? 502 : 200;
        (void)protocol::ReadIntegerField(payload, "status", &status);
        if (status < 100 || status > 599) {
            status = 502;
        }
        std::string content_type = "application/json; charset=utf-8";
        (void)protocol::ReadStringField(payload, "content_type", &content_type);
        std::string body;
        std::string message;
        if (type == "chat_response") {
            if (!protocol::ReadStringField(payload, "body", &body)) {
                message = "server response has no body";
                status = 502;
            }
        } else if (!protocol::ReadStringField(payload, "message", &message)) {
            message = "local model server returned an error";
        }
        CompletePending(pending, status, content_type, body, message);
        return true;
    }

    bool SendToServer(const std::string &payload, std::string *error)
    {
        std::lock_guard<std::mutex> write_lock(reverse_write_mutex_);
        std::lock_guard<std::mutex> reverse_lock(reverse_mutex_);
        if (reverse_fd_ < 0) {
            *error = "local model server is not connected";
            return false;
        }
        return SendFrameToSocket(reverse_fd_, payload, error);
    }

    void RemovePending(const std::string &request_id, const std::shared_ptr<PendingRequest> &expected)
    {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        const auto found = pending_.find(request_id);
        if (found != pending_.end() && found->second == expected) {
            pending_.erase(found);
        }
    }

    static void CompletePending(const std::shared_ptr<PendingRequest> &pending, int status,
                                const std::string &content_type, const std::string &body,
                                const std::string &error)
    {
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->status = status;
            pending->content_type = content_type;
            pending->body = body;
            pending->error = error;
            pending->done = true;
        }
        pending->condition.notify_all();
    }

    void FailAllPending(const std::string &message)
    {
        std::vector<std::shared_ptr<PendingRequest>> requests;
        {
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            for (const auto &entry : pending_) {
                requests.push_back(entry.second);
            }
            pending_.clear();
        }
        for (const auto &pending : requests) {
            CompletePending(pending, 502, "application/json; charset=utf-8", "", message);
        }
    }

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex status_mutex_;
    mutable std::mutex pending_mutex_;
    std::mutex reverse_mutex_;
    std::mutex reverse_write_mutex_;
    std::mutex http_clients_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> next_request_id_{1};
    std::atomic<std::size_t> active_http_count_{0};
    ProxyOptions options_;
    bool server_connected_ = false;
    std::string last_error_;
    int http_listener_fd_ = -1;
    int reverse_listener_fd_ = -1;
    int reverse_fd_ = -1;
    std::thread http_accept_thread_;
    std::thread reverse_thread_;
    std::vector<std::thread> http_workers_;
    std::unordered_set<int> http_client_fds_;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_;
};

ProxyGateway &ProxyGateway::Instance()
{
    static ProxyGateway instance;
    return instance;
}

ProxyGateway::ProxyGateway() : impl_(std::make_unique<Impl>()) {}

ProxyGateway::~ProxyGateway() = default;

bool ProxyGateway::Start(const ProxyOptions &options, std::string *error)
{
    return impl_->Start(options, error);
}

void ProxyGateway::Stop()
{
    impl_->Stop();
}

ProxyStatus ProxyGateway::Status() const
{
    return impl_->Status();
}

}  // namespace appless::lite_proxy
