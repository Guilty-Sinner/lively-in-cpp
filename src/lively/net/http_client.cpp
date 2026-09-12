#include <lively/net/http_client.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <stdexcept>

#pragma comment(lib, "winhttp.lib")

namespace lively::net {

std::optional<UrlParts> split_url(const std::string& url) {
    // Minimal parser: scheme://host[:port]/path?query
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;
    const std::string scheme = url.substr(0, scheme_end);
    std::string rest = url.substr(scheme_end + 3);

    bool is_https;
    if (scheme == "https") is_https = true;
    else if (scheme == "http") is_https = false;
    else return std::nullopt;

    const auto slash = rest.find('/');
    const std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (path.empty()) path = "/";

    std::string host = hostport;
    INTERNET_PORT port = is_https ? 443 : 80;
    const auto colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        const std::string port_str = hostport.substr(colon + 1);
        if (!port_str.empty()) {
            try { port = static_cast<INTERNET_PORT>(std::stoi(port_str)); }
            catch (...) { return std::nullopt; }
        }
    }
    if (host.empty()) return std::nullopt;

    UrlParts parts;
    parts.is_https = is_https;
    parts.port = port;
    parts.host = std::wstring(host.begin(), host.end());
    parts.path = std::wstring(path.begin(), path.end());
    return parts;
}

namespace {

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size);
    return out;
}

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}


// RAII for WinHTTP handles.
struct HInternetCloser {
    void operator()(HINTERNET h) const { if (h) WinHttpCloseHandle(h); }
};
using HInternet = std::unique_ptr<void, HInternetCloser>;

} // namespace

HttpClient::HttpClient() {
    // C# HttpClient allows all certificates validated by the OS store; the
    // default WinHTTP flags match that behaviour.
    session_ = WinHttpOpen(L"Lively.Cpp/2.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

HttpClient::~HttpClient() {
    if (session_) WinHttpCloseHandle(session_);
}

HttpResponse HttpClient::send(const HttpRequest& request) const {
    std::string body;
    auto result = send_streaming(request, [&](const char* data, size_t len) {
        body.append(data, len);
        return true;
    });
    if (!result) throw std::runtime_error("http transport failure: " + request.url);
    // send_streaming fills status/headers; the payload arrives via the callback.
    result->body = std::move(body);
    return std::move(*result);
}

std::optional<HttpResponse> HttpClient::send_streaming(
    const HttpRequest& request,
    const std::function<bool(const char*, size_t)>& on_chunk,
    const std::function<bool(const HttpResponse&)>& on_headers) const {

    const auto parts = split_url(request.url);
    if (!parts) return std::nullopt;

    const HInternet connect(WinHttpConnect(session_, parts->host.c_str(), parts->port, 0));
    if (!connect) return std::nullopt;

    const std::wstring method = utf8_to_wide(request.method);
    const HInternet req(WinHttpOpenRequest(connect.get(), method.c_str(), parts->path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           parts->is_https ? WINHTTP_FLAG_SECURE : 0));
    if (!req) return std::nullopt;

    if (request.timeout_ms > 0) {
        WinHttpSetTimeouts(req.get(), request.timeout_ms, request.timeout_ms,
                           request.timeout_ms, request.timeout_ms);
    }

    // Request headers.
    std::wstring headers_w;
    for (const auto& [name, value] : request.headers) {
        headers_w += utf8_to_wide(name) + L": " + utf8_to_wide(value) + L"\r\n";
    }
    const LPVOID body_ptr = request.body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : reinterpret_cast<LPVOID>(const_cast<char*>(request.body.data()));
    const DWORD body_len = static_cast<DWORD>(request.body.size());

    if (!WinHttpSendRequest(req.get(),
                            headers_w.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers_w.c_str(),
                            static_cast<DWORD>(headers_w.size()),
                            body_ptr, body_len, body_len, 0)) {
        return std::nullopt;
    }
    if (!WinHttpReceiveResponse(req.get(), nullptr)) return std::nullopt;

    HttpResponse response;

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;  // no usable response status → treat as transport failure
    }
    response.status_code = static_cast<int>(status);

    // Collect response headers (status line and following).
    DWORD header_size = 0;
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &header_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (header_size > 0) {
        std::wstring raw(static_cast<size_t>(header_size), L'\0');
        if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &header_size,
                                WINHTTP_NO_HEADER_INDEX)) {
            std::istringstream stream(wide_to_utf8(raw));
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string name = lower_ascii(line.substr(0, colon));
                std::string value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == ' ')) value.erase(value.begin());
                response.headers[name] = value;
            }
        }
    }

    // Header-stage veto (C# EnsureSuccessStatusCode): stop before the payload.
    if (on_headers && !on_headers(response)) return response;

    // Body (chunked pull).
    char buffer[16384];
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &available)) break;
        if (available == 0) break;
        if (available > sizeof(buffer)) available = sizeof(buffer);
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), buffer, available, &read)) break;
        if (read == 0) break;
        if (on_chunk && !on_chunk(buffer, read)) {
            // Caller abort (C# cancellation). Close the connection abruptly.
            return std::nullopt;
        }
    }

    return response;
}

} // namespace lively::net
