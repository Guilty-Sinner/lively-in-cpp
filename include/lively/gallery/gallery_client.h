#pragma once
// Port of Lively.Gallery.Client (GalleryClient.cs, SearchQuery.cs,
// SearchQueryBuilder.cs) — the REST client for the Lively wallpaper gallery.
//
// C#-faithful behaviours preserved deliberately (verified in tests):
//   * BOTH AuthenticateGoogleAsync and AuthenticateGithubAsync POST to
//     `auth/google-token?code=...&provider=...` — the C# code reuses the
//     google endpoint for GitHub (only the provider query value differs).
//   * Search URL params are NOT url-encoded (tags joined with ","), matching
//     the C# string interpolation.
//   * `sortBy=` carries the enum NAME (C# enum ToString), e.g. "Trending".
//   * Auth callback loopback server listens on 127.0.0.1:43821 and answers
//     "Authenticated. You can close this window now".
//   * 401 after a refresh attempt throws UnauthorizedAccessException.
//   * Response body {"Errors":[...]} throws ApiException carrying the list.
//
// C# async methods map to coroutine-friendly blocking calls executed on
// caller threads (the UI-layer call-sites in C# already dispatched these to
// background threads).

#include <lively/events.h>
#include <lively/models/gallery.h>
#include <lively/net/http_client.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace lively::gallery {

// ---- Exceptions -------------------------------------------------------------

// C# UnauthorizedAccessException with message.
class UnauthorizedException : public std::runtime_error {
public:
    explicit UnauthorizedException(const std::string& message) : std::runtime_error(message) {}
};

// C# ApiException(response.Errors).
class ApiException : public std::runtime_error {
public:
    explicit ApiException(std::vector<std::string> errors)
        : std::runtime_error(errors.empty() ? std::string("API error") : errors.front()),
          errors_(std::move(errors)) {}

    const std::vector<std::string>& errors() const { return errors_; }
    bool contains(const std::string& code) const {
        for (const auto& e : errors_) if (e == code) return true;
        return false;
    }

private:
    std::vector<std::string> errors_;
};

// ---- Token store ------------------------------------------------------------

// C# ITokenStore (in the real app: encrypted persistent storage). The Get/Set
// contract is what GalleryClient depends on; a memory implementation is
// provided, callers may inject a file-backed one.
class ITokenStore {
public:
    virtual ~ITokenStore() = default;
    virtual models::gallery::TokensModel get() const = 0;
    virtual void set(const std::string& access_token, const std::string& refresh_token,
                     const std::string& provider, const std::string& expiration_iso) = 0;
};

class MemoryTokenStore : public ITokenStore {
public:
    models::gallery::TokensModel get() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return tokens_;
    }
    void set(const std::string& access_token, const std::string& refresh_token,
             const std::string& provider, const std::string& expiration_iso) override {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_.access_token = access_token.empty() ? std::nullopt : std::optional(access_token);
        tokens_.refresh_token = refresh_token.empty() ? std::nullopt : std::optional(refresh_token);
        tokens_.provider = provider.empty() ? std::nullopt : std::optional(provider);
        tokens_.expiration_iso = expiration_iso;
    }

private:
    mutable std::mutex mutex_;
    models::gallery::TokensModel tokens_;
};

// ---- Search query (SearchQuery.cs + SearchQueryBuilder.cs) -------------------

class SearchQuery {
public:
    SearchQuery(std::optional<std::vector<std::string>> tags, std::optional<std::string> name,
                int page, int limit, models::gallery::SortingType sorting_type)
        : tags_(std::move(tags)), name_(std::move(name)), page_(page), limit_(limit),
          sorting_type_(sorting_type) {}

    int limit() const { return limit_; }
    models::gallery::SortingType sorting_type() const { return sorting_type_; }
    const std::optional<std::vector<std::string>>& tags() const { return tags_; }
    const std::optional<std::string>& name() const { return name_; }
    int page() const { return page_; }

private:
    std::optional<std::vector<std::string>> tags_;
    std::optional<std::string> name_;
    int page_;
    int limit_;
    models::gallery::SortingType sorting_type_;
};

// Defaults per the C# ctor (code, not the stale doc comment): page 0, limit 25,
// SortingType.Trending.
class SearchQueryBuilder {
public:
    SearchQueryBuilder& sort_by(models::gallery::SortingType sort) {
        sorting_type_ = sort;
        return *this;
    }
    SearchQueryBuilder& with_tags(std::vector<std::string> tags) {
        for (auto& t : tags) tags_.push_back(std::move(t));
        return *this;
    }
    SearchQueryBuilder& set_page(int page) {
        if (page < 0)
            throw std::out_of_range("page should be more than or equals 0");
        page_ = page;
        return *this;
    }
    SearchQueryBuilder& set_limit(int limit) {
        if (limit <= 0)
            throw std::out_of_range("limit should be more than 0");
        limit_ = limit;
        return *this;
    }
    SearchQueryBuilder& with_search_query(std::string name) {
        if (name.empty())
            throw std::invalid_argument("name");
        name_ = std::move(name);
        return *this;
    }
    SearchQuery build() const {
        // C#: tags passed only when non-empty (else null).
        return SearchQuery(tags_.empty() ? std::nullopt : std::optional(tags_),
                           name_, page_, limit_, sorting_type_);
    }

private:
    std::optional<std::string> name_;
    std::vector<std::string> tags_;
    models::gallery::SortingType sorting_type_ = models::gallery::SortingType::trending;
    int page_ = 0;
    int limit_ = 25;
};

// ---- The client --------------------------------------------------------------

class GalleryClient {
public:
    // C# ctor(host, authLink, githubAuthLink, tokenStore). An HttpClient is
    // created internally (IHttpClientFactory.CreateClient equivalent).
    GalleryClient(std::string host, std::string auth_link, std::string github_auth_link,
                  std::shared_ptr<ITokenStore> token_store);

    const std::string& host() const { return host_; }

    std::optional<models::gallery::ProfileDto> current_user() const;
    void set_current_user(models::gallery::ProfileDto user);
    models::gallery::TokensModel tokens() const { return token_store_->get(); }
    // C# IsLoggedIn: CurrentUser != null && Expiration > UtcNow.
    bool is_logged_in() const;

    // C# InitializeAsync: fetch /users/@me, store it as CurrentUser (even when
    // null) and raise LoggedIn on success. This is what populates CurrentUser —
    // GetMeAsync alone does NOT touch it.
    void initialize();

    // Events (C# EventHandler).
    lively::event<std::string> wallpaper_unsubscribed;
    lively::event<std::string> wallpaper_subscribed;
    lively::event<void> logged_in;
    lively::event<void> logged_out;

    // ---- Users ----
    std::optional<models::gallery::ProfileDto> get_me();
    std::optional<std::string> delete_account();

    // ---- Authentication ----
    // Opens the loopback callback server + returns the one-time code captured
    // from the browser redirect (C# RequestCodeAsync). `open_browser` mirrors
    // LinkUtil.OpenBrowser as an injectable for testability.
    using BrowserOpener = std::function<void(const std::string& url)>;
    std::string request_code(const std::string& provider,
                             const BrowserOpener& open_browser = {});
    models::gallery::TokensModel authenticate_google(const std::string& google_code);
    models::gallery::TokensModel authenticate_github(const std::string& github_code);
    bool logout();

    // ---- Gallery ----
    void download_wallpaper(const std::string& id, const std::string& file_name,
                            const std::function<void(float, float, float)>& progress_callback = {});
    // Payload captured by the streaming layer (spelled out in the header so
    // the progress accounting stays identical to the C# FileStream loop).
    std::string download_sink() const { return sink_; }
    models::gallery::WallpaperDto upload_wallpaper(const std::string& zip_path);
    models::gallery::WallpaperDto get_wallpaper_info(const std::string& id);
    models::gallery::WallpaperPage search_wallpapers(const SearchQuery& query);

    // ---- Subscriptions ----
    std::vector<models::gallery::WallpaperDto> get_wallpaper_subscriptions();
    bool subscribe_to_wallpaper(const std::string& id);   // rethrows after AlreadySubscribed event
    bool unsubscribe_from_wallpaper(const std::string& id);

    // ---- Other ----
    // C# GetBackendHealthAsync: no auth; null when the status is not success.
    std::optional<models::gallery::HealthResult> get_backend_health();

private:
    struct RawResponse {
        int status = 0;
        std::string body;
    };
    // InternalSendAsync + SendAsync combined; is_retry guards one refresh pass.
    RawResponse internal_send(const std::string& method, const std::string& url_suffix,
                              const std::string& body = {},
                              const std::map<std::string, std::string>& headers = {},
                              bool require_auth = true, bool is_retry = false,
                              const std::function<bool(const char*, size_t)>& on_chunk = {});
    template <typename T>
    models::gallery::ApiResponse<T> send(const std::string& method, const std::string& url_suffix,
                                         bool require_auth = true, bool is_retry = false,
                                         const std::string& body = {},
                                         const std::map<std::string, std::string>& headers = {});
    models::gallery::TokensModel refresh_tokens();
    void assign_media_urls(models::gallery::WallpaperDto& dto) const;
    static std::string sorting_type_name(models::gallery::SortingType type);

    std::string host_;
    std::string auth_link_;
    std::string github_auth_link_;
    std::shared_ptr<ITokenStore> token_store_;
    net::HttpClient http_;

    mutable std::mutex state_mutex_;
    std::optional<models::gallery::ProfileDto> current_user_;
    std::string one_time_auth_code_;
    std::string sink_;  // download buffer (C# FileStream target)
};

} // namespace lively::gallery
