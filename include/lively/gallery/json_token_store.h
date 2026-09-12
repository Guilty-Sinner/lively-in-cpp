#pragma once
// Port of Lively.Gallery.Client/JsonTokenStore.cs — the file-backed ITokenStore
// the real app injects into GalleryClient.
//
// C#:
//   private TokensModel _tokens;                    // lazy cache
//   Get()   { if (_tokens == null) try { _tokens = EncryptUtil.Load<TokensModel>(TokensPath); } catch { } return _tokens; }
//   Set(..) { _tokens = new() { ... }; try { EncryptUtil.Store(_tokens, TokensPath); } catch { } }
//   Clear() { _tokens = new(); try { EncryptUtil.Store(_tokens, TokensPath); } catch { } }
//
// Three behaviours that are load-bearing and easy to lose in a port:
//
//   * The file is written through EncryptUtil, i.e. DPAPI-protected with the
//     app alias as entropy, stored as a JSON string of base64 — NOT plaintext
//     JSON. A port that "helpfully" wrote JSON here would leave the user's
//     access token in cleartext on disk.
//   * Every I/O failure is swallowed (`catch { }`). A logged-out user with no
//     Tokens.dat, a corrupt blob, or a file written by another Windows account
//     must all degrade to "not logged in", never to a crash at startup.
//   * C# keeps the in-memory copy, so Get() after Set() does not touch the
//     disk. The port keeps the same cache and the same write-through.
//
// Deliberate hardening (not observable in the file format): the cache is
// mutex-guarded. The C# class is not thread-safe, but the port's core calls the
// token store from both the UI and the gRPC threads.

#include <lively/gallery/gallery_client.h>

#include <mutex>
#include <optional>
#include <string>

namespace lively::gallery {

class JsonTokenStore : public ITokenStore {
public:
    // CommonPaths.TokensPath — %LOCALAPPDATA%\Lively Wallpaper\Tokens.dat.
    JsonTokenStore();
    explicit JsonTokenStore(std::string path);

    // Never throws: a missing/corrupt token file reads as the default
    // TokensModel (C# would return null here; a missing AccessToken produces
    // the same UnauthorizedException downstream).
    models::gallery::TokensModel get() const override;

    // Writes through to disk, ignoring I/O failures like the C# `catch { }`.
    void set(const std::string& access_token, const std::string& refresh_token,
             const std::string& provider, const std::string& expiration_iso) override;

    void clear() override;

    const std::string& path() const { return path_; }

private:
    std::string path_;
    mutable std::mutex mutex_;
    // std::nullopt == the C# null `_tokens` (not yet loaded).
    mutable std::optional<models::gallery::TokensModel> cache_;
};

} // namespace lively::gallery
