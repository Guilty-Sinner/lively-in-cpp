#pragma once
// HTTP layer for the ported services (GalleryClient, GithubUpdaterService,
// HttpDownloadService).
//
// C# used System.Net.Http.HttpClient (+ IHttpClientFactory). The closest
// native Windows equivalent is WinHTTP: TLS, redirects and chunked responses
// handled by the OS, no third-party dependency. Sync mode only — the ported
// service methods that were `async` in C# run on coroutine threads / jthreads
// at the call-sites, and none of them issue concurrent requests on one client.
//
// Semantics preserved from the C# call-sites:
//   * 429/5xx are NOT retried (C# HttpClient does not retry either).
//   * Response headers read first (HttpCompletionOption.ResponseHeadersRead)
//     for downloads; callers get a stream-like chunked reader.
//   * Bearer auth is set by the caller per-request (GalleryClient).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace lively::net {

struct HttpResponse {
    int status_code = 0;
    std::map<std::string, std::string> headers;  // lower-cased names
    std::string body;

    bool is_success() const { return status_code >= 200 && status_code < 300; }
    std::optional<std::string> header(const std::string& lower_name) const {
        auto it = headers.find(lower_name);
        if (it == headers.end()) return std::nullopt;
        return it->second;
    }
};

struct HttpRequest {
    std::string method = "GET";             // "GET", "POST", "PUT", "DELETE"
    std::string url;                        // absolute http(s) URL
    std::map<std::string, std::string> headers;
    std::string body;                       // request payload (POST/PUT)
    // Milliseconds; 0 -> WinHTTP defaults. C# HttpClient default is 100s.
    uint32_t timeout_ms = 100000;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Full round-trip; body fully buffered (C# ResponseContentRead).
    HttpResponse send(const HttpRequest& request) const;

    // ResponseHeadersRead variant: status/headers returned, then the caller
    // pulls the payload in chunks (download progress reporting). `on_chunk`
    // returns false to abort (maps to C# CancellationToken).
    //
    // `on_headers` (optional) runs once the status line is known but before any
    // payload is read; returning false stops there and hands back the response
    // with an empty body — this is how C#'s EnsureSuccessStatusCode() behaves
    // for non-2xx responses.
    // Returns nullopt only on transport failure (no response at all).
    std::optional<HttpResponse> send_streaming(
        const HttpRequest& request,
        const std::function<bool(const char* data, size_t len)>& on_chunk,
        const std::function<bool(const HttpResponse& headers)>& on_headers = {}) const;

private:
    HINTERNET session_ = nullptr;
};

// Parse "https://host:port/path?query" into WinHTTP components.
struct UrlParts {
    std::wstring host;
    INTERNET_PORT port = 443;
    bool is_https = true;
    std::wstring path;   // includes query string
};
std::optional<UrlParts> split_url(const std::string& url);

} // namespace lively::net
