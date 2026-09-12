#include <lively/common/encrypt_util.h>

#include <lively/common/base64.h>
#include <lively/common/constants.h>
#include <lively/common/json_format.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace lively::common {

namespace {

using Bytes = std::vector<unsigned char>;

// Constants.SingleInstance.UniqueAppName as UTF-8 — the DPAPI entropy.
// C#: Encoding.UTF8.GetBytes(Constants.SingleInstance.UniqueAppName).
// Deliberately computed here rather than shared with the constants header: the
// C# static field is initialized once and is part of the on-disk contract, so
// the value must not be refactored away by accident.
const Bytes& entropy() {
    static const Bytes value = [] {
        const std::string alias = "LIVELY:DESKTOPWALLPAPERSYSTEM";
        return Bytes(alias.begin(), alias.end());
    }();
    return value;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// C# JsonStorage<byte[]>.StoreData: Newtonsoft writes a byte[] as a base64
// *string* (a scalar, so Formatting.Indented changes nothing here). The
// `jsonstorage.bytes` oracle line pins it: "\"AQID/wAQgA==\"" and an empty
// array as "\"\"". nlohmann's dump() of the string produces the same bytes.
void store_bytes(const std::string& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open file: " + path);
    out << nlohmann::ordered_json(base64_encode(data)).dump();
}

// C# JsonStorage<byte[]>.LoadData. Newtonsoft throws on a file that is not a
// JSON string; the port reports the same by throwing.
Bytes load_bytes(const std::string& path) {
    const std::string text = read_file(path);
    nlohmann::ordered_json parsed;
    try {
        parsed = nlohmann::ordered_json::parse(text);
    } catch (const std::exception&) {
        throw std::runtime_error("json null/corrupt");
    }
    if (!parsed.is_string()) throw std::runtime_error("json null/corrupt");
    const auto decoded = base64_decode(parsed.get<std::string>());
    if (!decoded) throw std::runtime_error("invalid base64 in " + path);
    return *decoded;
}

// EncryptUtil.Protect<T> is UTF8(JsonConvert.SerializeObject(data)) — COMPACT
// Newtonsoft JSON. TokensModel::to_json() builds the members in declaration
// order (AccessToken, RefreshToken, Provider, Expiration), which is the order
// the `tokenstore.plaintext` oracle line pins.
std::string tokens_plaintext(const models::gallery::TokensModel& tokens) {
    return tokens.to_json().dump();
}

models::gallery::TokensModel tokens_from_plaintext(const std::string& text) {
    return models::gallery::TokensModel::from_json(nlohmann::ordered_json::parse(text));
}

} // namespace

std::vector<unsigned char> protect_data(const std::vector<unsigned char>& data) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(data.data());
    in.cbData = static_cast<DWORD>(data.size());

    const Bytes& ent = entropy();
    DATA_BLOB entropy_blob{};
    entropy_blob.pbData = const_cast<BYTE*>(ent.data());
    entropy_blob.cbData = static_cast<DWORD>(ent.size());

    DATA_BLOB out{};
    // szDataDescr: C# passes null (no description blob).
    if (!CryptProtectData(&in, nullptr, &entropy_blob, nullptr, nullptr, 0, &out)) {
        throw std::runtime_error("CryptProtectData failed: " +
                                 std::to_string(static_cast<unsigned long>(GetLastError())));
    }
    Bytes result(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    (void)data;
    throw std::runtime_error("DPAPI is only available on Windows");
#endif
}

std::vector<unsigned char> unprotect_data(const std::vector<unsigned char>& data) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(data.data());
    in.cbData = static_cast<DWORD>(data.size());

    const Bytes& ent = entropy();
    DATA_BLOB entropy_blob{};
    entropy_blob.pbData = const_cast<BYTE*>(ent.data());
    entropy_blob.cbData = static_cast<DWORD>(ent.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy_blob, nullptr, nullptr, 0, &out)) {
        // The expected failure for a blob written by a different user/entropy —
        // C# surfaces CryptographicException and every caller swallows it.
        throw std::runtime_error("CryptUnprotectData failed: " +
                                 std::to_string(static_cast<unsigned long>(GetLastError())));
    }
    Bytes result(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    (void)data;
    throw std::runtime_error("DPAPI is only available on Windows");
#endif
}

void store_tokens(const std::string& file_path, const models::gallery::TokensModel& tokens) {
    // EncryptUtil.Store<T>: Protect(UTF8(JsonConvert.SerializeObject(data))),
    // then JsonStorage<byte[]>.StoreData. The plaintext is compact Newtonsoft
    // JSON — `tokenstore.plaintext` in tests/goldens/persist_csharp.txt pins it,
    // including that an unset Expiration serializes as "0001-01-01T00:00:00"
    // (no trailing Z, zero fraction trimmed).
    const std::string plaintext = tokens_plaintext(tokens);
    const Bytes utf8(plaintext.begin(), plaintext.end());
    store_bytes(file_path, protect_data(utf8));
}

models::gallery::TokensModel load_tokens(const std::string& file_path) {
    const Bytes plaintext = unprotect_data(load_bytes(file_path));
    const std::string text(plaintext.begin(), plaintext.end());
    return tokens_from_plaintext(text);
}

} // namespace lively::common
