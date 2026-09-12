#include <lively/gallery/json_token_store.h>

#include <lively/common/constants.h>
#include <lively/common/encrypt_util.h>
#include <lively/common/path_util.h>

#include <utility>

namespace lively::gallery {

JsonTokenStore::JsonTokenStore()
    : JsonTokenStore(common::path::utf8_from_wide(common::common_paths::TokensPath())) {}

JsonTokenStore::JsonTokenStore(std::string path) : path_(std::move(path)) {}

models::gallery::TokensModel JsonTokenStore::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cache_) {
        // C# `try { _tokens = EncryptUtil.Load<TokensModel>(TokensPath); } catch { }`
        // — note this also caches the failure, so a second call does not retry.
        try {
            cache_ = common::load_tokens(path_);
        } catch (const std::exception&) {
            cache_ = models::gallery::TokensModel{};
        }
    }
    return *cache_;
}

void JsonTokenStore::set(const std::string& access_token, const std::string& refresh_token,
                         const std::string& provider, const std::string& expiration_iso) {
    models::gallery::TokensModel tokens;
    // C# assigns the strings directly (a null argument stays null); the port's
    // empty string maps to the same absent state the rest of the port uses.
    tokens.access_token = access_token.empty() ? std::nullopt : std::optional(access_token);
    tokens.refresh_token = refresh_token.empty() ? std::nullopt : std::optional(refresh_token);
    tokens.provider = provider.empty() ? std::nullopt : std::optional(provider);
    tokens.expiration_iso = expiration_iso;

    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = tokens;
    try {
        common::store_tokens(path_, tokens);
    } catch (const std::exception&) {
        // C# swallows this too: the in-memory session still works for as long as
        // the process lives.
    }
}

void JsonTokenStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = models::gallery::TokensModel{};
    try {
        common::store_tokens(path_, *cache_);
    } catch (const std::exception&) {
    }
}

} // namespace lively::gallery
