#include <lively/gallery/gallery_client.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace lively::gallery {

namespace {

using Json = nlohmann::ordered_json;

// "object?" deserialization target — keeps the raw payload.
struct AnyJson {
    Json value;
    static AnyJson from_json(const Json& j) { return AnyJson{j}; }
};

// Days since 1970-01-01 for a proleptic Gregorian date (Hinnant's algorithm).
// _mkgmtime() returns -1 for far-future years (it rejects year 9999), which
// would make a perfectly valid token read as expired, so do the conversion
// ourselves over a 64-bit range.
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool expiration_in_future(const std::string& iso) {
    std::tm tm_utc{};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return false;  // unparseable → treat as not logged in (C# MinValue)

    const std::int64_t days = days_from_civil(tm_utc.tm_year + 1900,
                                              static_cast<unsigned>(tm_utc.tm_mon + 1),
                                              static_cast<unsigned>(tm_utc.tm_mday));
    const std::int64_t seconds = days * 86400 + tm_utc.tm_hour * 3600 + tm_utc.tm_min * 60 + tm_utc.tm_sec;
    return seconds > static_cast<std::int64_t>(std::time(nullptr));
}

std::string upper_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Loopback auth-callback server (C# WatsonWebserver on 127.0.0.1:43821).
class LoopbackAuthServer {
public:
    bool start(uint16_t port) {
        SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;

        BOOL reuse = TRUE;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
            ::listen(sock, 4) == SOCKET_ERROR) {
            ::closesocket(sock);
            return false;
        }
        socket_ = sock;
        running_ = true;
        thread_ = std::jthread([this] { accept_loop(); });
        return true;
    }

    void stop() {
        running_ = false;
        if (socket_ != INVALID_SOCKET) {
            ::shutdown(socket_, SD_BOTH);
            ::closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) thread_.join();
    }

    std::string wait_for_code() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !code_.empty(); });
        return code_;
    }

    ~LoopbackAuthServer() { stop(); }

private:
    void accept_loop() {
        while (running_) {
            SOCKET client = ::accept(socket_, nullptr, nullptr);
            if (client == INVALID_SOCKET) break;
            std::string request;
            char buf[4096];
            for (;;) {
                const int n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                request.append(buf, static_cast<size_t>(n));
                if (request.find("\r\n\r\n") != std::string::npos || request.size() > 65536) break;
            }
            // Extract ?code= from the request line (C# Query.Elements["code"]).
            std::string code;
            const auto line_end = request.find("\r\n");
            const std::string request_line =
                line_end == std::string::npos ? request : request.substr(0, line_end);
            const auto q = request_line.find("?code=");
            if (q != std::string::npos) {
                code = request_line.substr(q + 6);
                if (const auto amp = code.find('&'); amp != std::string::npos) code = code.substr(0, amp);
                if (const auto sp = code.find(' '); sp != std::string::npos) code = code.substr(0, sp);
            }
            const char* response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n"
                "Content-Length: 46\r\n\r\n"
                "Authenticated. You can close this window now\n";
            ::send(client, response, static_cast<int>(std::strlen(response)), 0);
            ::shutdown(client, SD_SEND);
            ::closesocket(client);

            if (!code.empty()) {
                {
                    std::lock_guard lock(mutex_);
                    code_ = code;
                }
                cv_.notify_all();
                break;
            }
        }
    }

    SOCKET socket_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::jthread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string code_;
};

} // namespace

GalleryClient::GalleryClient(std::string host, std::string auth_link, std::string github_auth_link,
                             std::shared_ptr<ITokenStore> token_store)
    : host_(host.back() == '/' ? host.substr(0, host.size() - 1) : std::move(host)),
      auth_link_(std::move(auth_link)),
      github_auth_link_(std::move(github_auth_link)),
      token_store_(std::move(token_store)) {
    WSADATA wsa{};
    static const bool wsa_init = [&] { return WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }();
    (void)wsa_init;
}

std::optional<models::gallery::ProfileDto> GalleryClient::current_user() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_user_;
}

void GalleryClient::set_current_user(models::gallery::ProfileDto user) {
    std::lock_guard lock(state_mutex_);
    current_user_ = std::move(user);
}

bool GalleryClient::is_logged_in() const {
    std::lock_guard lock(state_mutex_);
    if (!current_user_) return false;
    return expiration_in_future(token_store_->get().expiration_iso);
}

void GalleryClient::initialize() {
    auto me = get_me();
    bool logged_in_now = false;
    {
        std::lock_guard lock(state_mutex_);
        current_user_ = std::move(me);
        logged_in_now = current_user_.has_value();
    }
    // Raised outside the lock: handlers may call back into the client.
    if (logged_in_now) logged_in.raise();
}

std::string GalleryClient::sorting_type_name(models::gallery::SortingType type) {
    switch (type) {
        case models::gallery::SortingType::all_time_top: return "AllTimeTop";
        case models::gallery::SortingType::newest: return "Newest";
        case models::gallery::SortingType::trending: return "Trending";
    }
    return "Trending";
}

GalleryClient::RawResponse GalleryClient::internal_send(
    const std::string& method, const std::string& url_suffix, const std::string& body,
    const std::map<std::string, std::string>& extra_headers, bool require_auth, bool is_retry,
    const std::function<bool(const char*, size_t)>& on_chunk) {

    const auto current_tokens = token_store_->get();
    if (require_auth && !current_tokens.access_token)
        throw UnauthorizedException("Token not found.");

    net::HttpRequest request;
    request.method = method;
    request.url = host_ + url_suffix;
    if (require_auth)
        request.headers["Authorization"] = "Bearer " + *current_tokens.access_token;
    for (const auto& [k, v] : extra_headers) request.headers[k] = v;
    if (!body.empty()) {
        request.headers["Content-Type"] = "application/json";
        request.body = body;
    }

    const auto raw = [&] {
        if (on_chunk) return http_.send_streaming(request, on_chunk);
        return std::optional<net::HttpResponse>(http_.send(request));
    }();
    if (!raw) throw std::runtime_error("gallery request failed: " + request.url);

    // 401 → one refresh+retry pass (C# InternalSendAsync).
    if (!is_retry && raw->status_code == 401) {
        const auto refreshed = refresh_tokens();
        const auto provider = token_store_->get().provider;
        token_store_->set(refreshed.access_token.value_or(""),
                          refreshed.refresh_token.value_or(""),
                          provider.value_or(""),
                          refreshed.expiration_iso);
        return internal_send(method, url_suffix, body, extra_headers, require_auth, true, on_chunk);
    }

    return RawResponse{raw->status_code, raw->body};
}

models::gallery::TokensModel GalleryClient::refresh_tokens() {
    const auto tokens = token_store_->get();
    if (!tokens.access_token || !tokens.refresh_token)
        throw UnauthorizedException("Couldn't refresh tokens. You have to log in again");

    // C#: SendAsync<TokensModel>(message, requireAuth: false, isRetry: true)
    // then `return result.Data` — the API envelope MUST be unwrapped, exactly
    // like every other call. (Reading AccessToken off the envelope top level
    // silently produced empty tokens and a bogus "Token not found.".)
    const auto result = send<models::gallery::TokensModel>(
        "POST", "/auth/refresh", /*require_auth=*/false, /*is_retry=*/true,
        tokens.to_json().dump());

    // C# would dereference a null Data here (NRE); surface it as the same
    // authorization failure the retry pass would hit instead.
    if (!result.data) throw UnauthorizedException(models::gallery::ApiErrors::TokensExpired);
    return *result.data;
}

template <typename T>
models::gallery::ApiResponse<T> GalleryClient::send(
    const std::string& method, const std::string& url_suffix, bool require_auth, bool is_retry,
    const std::string& body, const std::map<std::string, std::string>& headers) {

    const auto raw = internal_send(method, url_suffix, body, headers, require_auth, is_retry, {});
    if (raw.status == 401)
        throw UnauthorizedException(models::gallery::ApiErrors::TokensExpired);

    models::gallery::ApiResponse<T> response;
    response.status_code = raw.status;

    if (raw.body.empty()) {
        response.success = raw.status >= 200 && raw.status < 300;
        return response;
    }

    const auto parsed = Json::parse(raw.body, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        if (parsed.contains("Success")) response.success = parsed.at("Success").get<bool>();
        if (parsed.contains("Errors") && !parsed.at("Errors").is_null()) {
            std::vector<std::string> errors;
            for (const auto& e : parsed.at("Errors")) errors.push_back(e.get<std::string>());
            response.errors = errors;
        }
        if (parsed.contains("Data") && !parsed.at("Data").is_null())
            response.data = T::from_json(parsed.at("Data"));
    }
    if (response.errors)
        throw ApiException(*response.errors);
    return response;
}

std::optional<models::gallery::ProfileDto> GalleryClient::get_me() {
    auto result = send<models::gallery::ProfileDto>("GET", "/users/@me");
    return result.data;
}

std::optional<std::string> GalleryClient::delete_account() {
    auto result = send<AnyJson>("DELETE", "/users/@me");
    if (!result.errors) {
        std::lock_guard lock(state_mutex_);
        current_user_ = std::nullopt;
        logged_out.raise();
        return std::nullopt;
    }
    return result.errors->empty() ? std::optional<std::string>("API error")
                                  : std::optional<std::string>(result.errors->front());
}

std::string GalleryClient::request_code(const std::string& provider,
                                        const BrowserOpener& open_browser) {
    const std::string provider_upper = upper_ascii(provider);

    LoopbackAuthServer server;
    if (!server.start(43821))
        throw std::runtime_error("cannot start auth loopback server on port 43821");

    const std::string* link = nullptr;
    if (provider_upper == "GITHUB") link = &github_auth_link_;
    else if (provider_upper == "GOOGLE") link = &auth_link_;
    else throw std::invalid_argument("provider");

    if (open_browser) open_browser(*link);

    one_time_auth_code_ = server.wait_for_code();
    return one_time_auth_code_;
}

models::gallery::TokensModel GalleryClient::authenticate_google(const std::string& google_code) {
    // C# sends BOTH providers to auth/google-token (deliberate quirk).
    auto result = send<models::gallery::TokensModel>(
        "POST", "/auth/google-token?code=" + google_code + "&provider=GOOGLE", false);
    const auto tokens = result.data;
    if (tokens) {
        token_store_->set(tokens->access_token.value_or(""), tokens->refresh_token.value_or(""),
                          "GOOGLE", tokens->expiration_iso);
        if (auto me = get_me()) {
            std::lock_guard lock(state_mutex_);
            current_user_ = *me;
            logged_in.raise();
        }
    }
    return tokens ? *tokens : models::gallery::TokensModel{};
}

models::gallery::TokensModel GalleryClient::authenticate_github(const std::string& github_code) {
    auto result = send<models::gallery::TokensModel>(
        "POST", "/auth/google-token?code=" + github_code + "&provider=GITHUB", false);
    const auto tokens = result.data;
    if (tokens) {
        token_store_->set(tokens->access_token.value_or(""), tokens->refresh_token.value_or(""),
                          "GITHUB", tokens->expiration_iso);
        if (auto me = get_me()) {
            std::lock_guard lock(state_mutex_);
            current_user_ = *me;
            logged_in.raise();
        }
    }
    return tokens ? *tokens : models::gallery::TokensModel{};
}

bool GalleryClient::logout() {
    auto result = send<AnyJson>("GET", "/auth/logout");
    {
        std::lock_guard lock(state_mutex_);
        current_user_ = std::nullopt;
    }
    // C# `_tokenStore.Set(null, null, null, DateTime.MinValue)`. The timestamp is
    // Newtonsoft's canonical rendering of default(DateTime): NO zone suffix —
    // "0001-01-01T00:00:00Z" would be a different string and would make the
    // persisted Tokens.dat plaintext diverge from the C# app's.
    token_store_->set("", "", "", "0001-01-01T00:00:00");
    logged_out.raise();
    return result.success;
}

void GalleryClient::assign_media_urls(models::gallery::WallpaperDto& dto) const {
    // C#: Preview only when available; Thumbnail always (both under Host).
    dto.preview = dto.is_preview_available
        ? std::optional(host_ + "/gallery/" + dto.id.value_or("") + "/preview")
        : std::nullopt;
    dto.thumbnail = host_ + "/gallery/" + dto.id.value_or("") + "/thumbnail";
}

void GalleryClient::download_wallpaper(const std::string& id, const std::string& file_name,
                                       const std::function<void(float, float, float)>& progress_callback) {
    const auto raw = internal_send("GET", "/gallery/" + id + "/download", {}, {}, true, false,
                                   [&](const char* data, size_t len) {
                                       sink_.append(data, len);
                                       return true;
                                   });
    if (raw.status == 401)
        throw UnauthorizedException("Couldn't refresh tokens. You have to log in again");

    if (raw.status < 200 || raw.status >= 300) {
        // C# returns the status code as an error list which
        // DownloadWallpaperAsync discards → silent no-op, no file created.
        sink_.clear();
        return;
    }

    std::filesystem::path out(file_name);
    if (out.has_parent_path())
        std::filesystem::create_directories(out.parent_path());

    // Write captured payload with C# progress accounting:
    //   progress(0, 0, length) first, then on totalRead % 30 == 0, then 100.
    std::ofstream file(file_name, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot create file: " + file_name);

    const long long total = static_cast<long long>(sink_.size());
    if (progress_callback) progress_callback(0.f, 0.f, static_cast<float>(total));
    file.write(sink_.data(), static_cast<std::streamsize>(sink_.size()));
    // Per-read accounting (C# 8192-byte reads, progress every 30 reads).
    {
        long long written = 0;
        while (written < total) {
            const long long chunk = std::min<long long>(8192, total - written);
            written += chunk;
            if (progress_callback && written % 30 == 0)
                progress_callback(static_cast<float>(written / static_cast<double>(total) * 100.0),
                                  static_cast<float>(written), static_cast<float>(total));
        }
    }
    if (progress_callback && total > 0)
        progress_callback(100.f, static_cast<float>(total), static_cast<float>(total));
    sink_.clear();
}

models::gallery::WallpaperDto GalleryClient::upload_wallpaper(const std::string& /*zip_path*/) {
    // C# UploadWallpaperAsync posts multipart/form-data with a zip; requires
    // the archive pipeline (wallpaper metadata packing), landing with the UI
    // tranche. C# callers reach this only from ShareWallpaperViewModel.
    throw std::logic_error("upload requires the wallpaper archive pipeline (UI tranche)");
}

models::gallery::WallpaperDto GalleryClient::get_wallpaper_info(const std::string& id) {
    auto result = send<models::gallery::WallpaperDto>("GET", "/gallery/" + id);
    auto dto = result.data ? *result.data : models::gallery::WallpaperDto{};
    assign_media_urls(dto);
    return dto;
}

models::gallery::WallpaperPage GalleryClient::search_wallpapers(const SearchQuery& query) {
    // Deliberately not url-encoded (C# string interpolation parity).
    std::string uri = "/gallery/search?sortBy=" + sorting_type_name(query.sorting_type());
    uri += "&page=" + std::to_string(query.page());
    uri += "&perPage=" + std::to_string(query.limit());
    if (query.name())
        uri += "&query=" + *query.name();
    if (query.tags()) {
        std::string joined;
        for (const auto& t : *query.tags()) {
            if (!joined.empty()) joined += ",";
            joined += t;
        }
        uri += "&tags=" + joined;
    }

    const auto raw = internal_send("GET", uri, {}, {}, true, false, {});
    if (raw.status == 401)
        throw UnauthorizedException(models::gallery::ApiErrors::TokensExpired);
    const auto parsed = Json::parse(raw.body, nullptr, false);
    if (parsed.is_discarded()) throw std::runtime_error("invalid search response");
    if (parsed.contains("Errors") && !parsed.at("Errors").is_null()) {
        std::vector<std::string> errors;
        for (const auto& e : parsed.at("Errors")) errors.push_back(e.get<std::string>());
        throw ApiException(errors);
    }
    auto page = models::gallery::WallpaperPage::from_json(
        parsed.contains("Data") && !parsed.at("Data").is_null() ? parsed.at("Data") : Json::object());
    for (auto& item : page.data) assign_media_urls(item);
    return page;
}

std::vector<models::gallery::WallpaperDto> GalleryClient::get_wallpaper_subscriptions() {
    const auto raw = internal_send("GET", "/users/@me/wallpapers", {}, {}, true, false, {});
    const auto parsed = Json::parse(raw.body, nullptr, false);
    if (parsed.is_discarded()) throw std::runtime_error("invalid subscriptions response");
    std::vector<models::gallery::WallpaperDto> items;
    if (parsed.is_array()) {
        for (const auto& item : parsed) items.push_back(models::gallery::WallpaperDto::from_json(item));
    } else if (parsed.contains("Data") && parsed.at("Data").is_array()) {
        for (const auto& item : parsed.at("Data")) items.push_back(models::gallery::WallpaperDto::from_json(item));
    }
    for (auto& item : items) assign_media_urls(item);
    return items;
}

bool GalleryClient::subscribe_to_wallpaper(const std::string& id) {
    try {
        auto result = send<AnyJson>("PUT", "/users/@me/wallpapers/" + id);
        wallpaper_subscribed.raise(id);
        return result.success;
    } catch (const ApiException& e) {
        if (e.contains(models::gallery::ApiErrors::AlreadySubscribedToWallpaper))
            wallpaper_subscribed.raise(id);
        throw;
    }
}

bool GalleryClient::unsubscribe_from_wallpaper(const std::string& id) {
    auto result = send<AnyJson>("DELETE", "/users/@me/wallpapers/" + id);
    wallpaper_unsubscribed.raise(id);
    return result.success;
}

std::optional<models::gallery::HealthResult> GalleryClient::get_backend_health() {
    net::HttpRequest request;
    request.method = "GET";
    request.url = host_ + "/health";
    const auto resp = http_.send(request);
    if (!resp.is_success()) return std::nullopt;
    const auto parsed = Json::parse(resp.body, nullptr, false);
    if (parsed.is_discarded()) return std::nullopt;
    return models::gallery::HealthResult::from_json(parsed);
}

} // namespace lively::gallery
