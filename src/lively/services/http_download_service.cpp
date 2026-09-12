#include <lively/services/http_download_service.h>

#include <lively/net/http_client.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace lively::services {

namespace {
constexpr double kBytesPerMb = 1024.0 * 1024.0;
} // namespace

void HttpDownloadService::download_file(const std::string& url, const std::string& file_path,
                                        const ProgressFn& progress, const CancelFn& cancelled) {
    const net::HttpClient http;  // C# creates a client per factory call.

    const std::filesystem::path out(file_path);
    if (out.has_parent_path())
        std::filesystem::create_directories(out.parent_path());

    net::HttpRequest request;
    request.method = "GET";
    request.url = url;

    std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot create file: " + file_path);

    uint64_t downloaded = 0;
    double last_reported_mb = -1.0;
    bool cancelled_now = false;

    try {
        // C# used HttpClient.GetAsync, which calls EnsureSuccessStatusCode():
        // a non-2xx response throws HttpRequestException *before* any body is
        // consumed. Abort at the header stage so no error payload is written.
        auto response = http.send_streaming(request, [&](const char* data, size_t len) {
            if (cancelled && cancelled()) { cancelled_now = true; return false; }
            file.write(data, static_cast<std::streamsize>(len));
            if (file.fail()) throw std::runtime_error("write failed: " + file_path);
            downloaded += len;
            if (progress) {
                // C# reports TRUNCATED MB values; only on change.
                const double mb = std::trunc(static_cast<double>(downloaded) / kBytesPerMb);
                if (mb != last_reported_mb) {
                    last_reported_mb = mb;
                    const double total_mb = 0.0;  // C# reports Content-Length when known
                    progress(mb, total_mb);
                }
            }
            return true;
        }, [](const net::HttpResponse& headers) { return headers.is_success(); });

        if (!response) {
            if (cancelled_now) {
                file.close();
                std::error_code ec;
                std::filesystem::remove(file_path, ec);
                return;  // C# OperationCanceledException path: cleanup, no throw.
            }
            throw std::runtime_error("download failed (transport): " + url);
        }

        if (!response->is_success()) {
            // File deletion happens in the catch below (C# catch(Exception)).
            throw std::runtime_error("download failed (HTTP " +
                                     std::to_string(response->status_code) + "): " + url);
        }
    } catch (...) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(file_path, ec);  // C# catch: delete, rethrow.
        throw;
    }

    file.flush();
    if (file.fail()) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(file_path, ec);
        throw std::runtime_error("flush failed: " + file_path);
    }
}

} // namespace lively::services
