#pragma once
// Shared raw-HTTP test server for the gallery/services tests: one
// connection per request, ephemeral port, handler-decided responses.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace lively_test {

class TestHttpServer {
public:
    using Handler = std::function<std::string(
        const std::string& method, const std::string& target,
        const std::map<std::string, std::string>& headers, const std::string& body)>;

    explicit TestHttpServer(Handler handler) : handler_(std::move(handler)) {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        owns_wsa_ = true;

        listen_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listen_ != INVALID_SOCKET);
        BOOL reuse = TRUE;
        ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_, 8) == 0);

        int len = sizeof(addr);
        ::getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        thread_ = std::jthread([this] { run(); });
    }

    TestHttpServer(const TestHttpServer&) = delete;
    TestHttpServer& operator=(const TestHttpServer&) = delete;

    ~TestHttpServer() {
        running_ = false;
        if (listen_ != INVALID_SOCKET) ::closesocket(listen_);
        if (thread_.joinable()) thread_.join();
        if (owns_wsa_) WSACleanup();
    }

    uint16_t port() const { return port_; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    int request_count() const { return requests_.load(); }

    std::string last_header(const std::string& name) const {
        std::lock_guard lock(mutex_);
        const auto it = last_headers_.find(name);
        return it == last_headers_.end() ? std::string{} : it->second;
    }

    std::string last_body() const {
        std::lock_guard lock(mutex_);
        return last_body_;
    }

private:
    void run() {
        while (running_) {
            SOCKET client = ::accept(listen_, nullptr, nullptr);
            if (client == INVALID_SOCKET) break;
            std::string raw;
            char buf[8192];
            for (;;) {
                const int n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                raw.append(buf, n);
                if (raw.find("\r\n\r\n") != std::string::npos) break;
            }

            std::string method = "GET", target = "/";
            std::map<std::string, std::string> headers;
            std::string body;
            {
                const auto line_end = raw.find("\r\n");
                const std::string request_line =
                    raw.substr(0, line_end == std::string::npos ? raw.size() : line_end);
                const auto sp1 = request_line.find(' ');
                const auto sp2 = request_line.find(' ', sp1 + 1);
                if (sp1 != std::string::npos && sp2 != std::string::npos) {
                    method = request_line.substr(0, sp1);
                    target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
                }
                size_t pos = line_end == std::string::npos ? raw.size() : line_end + 2;
                while (pos < raw.size()) {
                    const auto eol = raw.find("\r\n", pos);
                    const std::string line =
                        raw.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
                    if (line.empty()) break;
                    const auto colon = line.find(':');
                    if (colon != std::string::npos)
                        headers[line.substr(0, colon)] = line.substr(colon + 2);
                    if (eol == std::string::npos) break;
                    pos = eol + 2;
                }
                const auto cl = headers.find("Content-Length");
                if (cl != headers.end()) {
                    const size_t body_start = raw.find("\r\n\r\n") + 4;
                    const size_t want = static_cast<size_t>(std::atoi(cl->second.c_str()));
                    while (raw.size() - body_start < want) {
                        const int n = ::recv(client, buf, sizeof(buf), 0);
                        if (n <= 0) break;
                        raw.append(buf, n);
                    }
                    body = raw.substr(body_start, want);
                }
            }

            ++requests_;
            {
                std::lock_guard lock(mutex_);
                last_headers_ = headers;
                last_body_ = body;
            }
            const std::string response = handler_(method, target, headers, body);
            ::send(client, response.data(), static_cast<int>(response.size()), 0);
            ::shutdown(client, SD_SEND);
            ::closesocket(client);
        }
    }

    SOCKET listen_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    bool owns_wsa_ = false;
    std::atomic<bool> running_{true};
    std::atomic<int> requests_{0};
    mutable std::mutex mutex_;
    std::map<std::string, std::string> last_headers_;
    std::string last_body_;
    std::jthread thread_;
    Handler handler_;
};

inline std::string http_response(int status, const std::string& body,
                                 const std::string& content_type = "application/json") {
    const char* reason = status == 200 ? "OK" : status == 401 ? "Unauthorized" : "Error";
    return "HTTP/1.1 " + std::to_string(status) + " " + reason +
           "\r\nContent-Type: " + content_type +
           "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace lively_test
