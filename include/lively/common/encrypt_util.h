#pragma once
// Port of Lively.Common/Helpers/EncryptUtil.cs — DPAPI-protected JSON.
//
// C#:
//   Protect(data)   = ProtectedData.Protect(data, entropy, CurrentUser)
//   Protect<T>(v)   = Protect(UTF8(JsonConvert.SerializeObject(v)))
//   Store<T>(v, p)  = JsonStorage<byte[]>.StoreData(p, Protect<T>(v))
//   Load<T>(p)      = DeserializeObject<T>(Unprotect(JsonStorage<byte[]>.LoadData(p)))
// with entropy = UTF8(Constants.SingleInstance.UniqueAppName).
//
// Two consequences worth stating plainly:
//
//   * The DPAPI blob is NOT deterministic (it carries a random key/salt), so
//     Tokens.dat can never be byte-compared between implementations. What IS
//     pinned by the oracle is the surrounding shape — a JSON *string* holding
//     base64 (tools/csharp_probe `persist`) — plus the plaintext that gets
//     encrypted, which is deterministic.
//   * Entropy is bound to the alias, so a blob written with one alias cannot be
//     read with another. Changing kUniqueAppName silently invalidates every
//     user's saved tokens; that is upstream behaviour, not a porting choice.

#include <lively/models/gallery.h>

#include <string>
#include <vector>

namespace lively::common {

// ProtectedData.Protect / Unprotect with DataProtectionScope.CurrentUser.
// Throws std::runtime_error when DPAPI refuses (wrong user, corrupt blob).
std::vector<unsigned char> protect_data(const std::vector<unsigned char>& data);
std::vector<unsigned char> unprotect_data(const std::vector<unsigned char>& data);

// EncryptUtil.Store<TokensModel> / Load<TokensModel> for Tokens.dat.
void store_tokens(const std::string& file_path, const models::gallery::TokensModel& tokens);

// Throws when the file is missing or cannot be decrypted (C# swallows the
// exception at the call-site; JsonTokenStore below does the same).
models::gallery::TokensModel load_tokens(const std::string& file_path);

} // namespace lively::common
