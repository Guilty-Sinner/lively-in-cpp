#include <lively/common/link_util.h>

#include <lively/common/path_util.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#endif

namespace lively::common {

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_whitespace_only(const std::string& s) {
    return s.find_first_not_of(" \t\r\n\f\v") == std::string::npos;
}

bool is_drive_absolute(const std::string& s) {
    return s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' &&
           (s[2] == '\\' || s[2] == '/');
}

bool is_valid_scheme_char(char c, bool first) {
    if (std::isalpha(static_cast<unsigned char>(c))) return true;
    if (!first && (std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')) {
        return true;
    }
    return false;
}

struct UriParts {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    std::string query;    // includes '#', since .NET splits at '?' and keeps the rest
    bool has_authority = false;
};

// Approximates `new Uri(text)` succeeding. Returns nullopt in the cases where
// the .NET constructor throws UriFormatException — a relative path, a rooted
// path with no drive letter, or a scheme without "//".
std::optional<UriParts> parse_uri(const std::string& text) {
    if (text.empty() || is_whitespace_only(text)) return std::nullopt;

    // A drive-qualified path is a file URI to .NET, and it has to be checked
    // before the scheme branch: "C:\x" would otherwise look like the scheme "C".
    if (is_drive_absolute(text)) {
        UriParts parts;
        parts.scheme = "file";
        parts.has_authority = true;
        parts.path = text;
        return parts;
    }

    // "scheme://" (hierarchical) or "file:" forms.
    const std::size_t colon = text.find(':');
    if (colon != std::string::npos && colon > 0) {
        bool scheme_ok = is_valid_scheme_char(text[0], true);
        for (std::size_t i = 1; scheme_ok && i < colon; ++i) {
            scheme_ok = is_valid_scheme_char(text[i], false);
        }
        if (scheme_ok) {
            UriParts parts;
            parts.scheme = to_lower(text.substr(0, colon));
            std::string rest = text.substr(colon + 1);
            if (rest.rfind("//", 0) == 0) {
                parts.has_authority = true;
                rest.erase(0, 2);
                // authority ends at the first '/', '?' or '#'
                const std::size_t end = rest.find_first_of("/?#");
                std::string authority =
                    end == std::string::npos ? rest : rest.substr(0, end);
                rest = end == std::string::npos ? std::string() : rest.substr(end);

                const std::size_t at = authority.find('@');
                if (at != std::string::npos) authority = authority.substr(at + 1); // drop userinfo
                const std::size_t port_at = authority.rfind(':');
                if (port_at != std::string::npos &&
                    authority.find(':') == port_at) { // not an IPv6 literal
                    parts.port = authority.substr(port_at + 1);
                    authority = authority.substr(0, port_at);
                }
                parts.host = to_lower(authority);
            } else if (parts.scheme == "file") {
                // file:C:/x forms are accepted by .NET as well.
                parts.has_authority = true;
                parts.host.clear();
            } else {
                return std::nullopt; // e.g. "mailto:x" style is not used by Lively
            }
            const std::size_t q = rest.find_first_of("?#");
            parts.path = q == std::string::npos ? rest : rest.substr(0, q);
            parts.query = q == std::string::npos ? std::string() : rest.substr(q);
            return parts;
        }
    }

    // No scheme and not a rooted path: relative paths ("local/path/file.html")
    // and rooted paths with no drive letter ("/local/path/file.html") throw in
    // .NET, so GetLastSegmentUrl returns them unchanged.
    return std::nullopt;
}

std::vector<std::string> last_segments(const std::string& path) {
    // Splitting on both separators matches the fact that .NET normalizes '\'
    // to '/' inside a Uri; empty segments are dropped the way Uri.Segments and
    // the C# `Replace("/", "")` combination end up behaving.
    std::vector<std::string> segments;
    std::string current;
    for (const char c : path) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) segments.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) segments.push_back(current);
    return segments;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    std::size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

#ifdef _WIN32
std::string sha1_first8_hex(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA1) failed");
    }
    DWORD object_size = 0, hash_size = 0, produced = 0;
    std::vector<unsigned char> object, hash;
    bool ok = ::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                                  &produced, 0) >= 0 &&
              ::BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                  reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &produced,
                                  0) >= 0;
    if (ok) {
        object.resize(object_size);
        hash.resize(hash_size);
        BCRYPT_HASH_HANDLE handle = nullptr;
        ok = ::BCryptCreateHash(algorithm, &handle, object.data(), object_size, nullptr, 0, 0) >= 0;
        if (ok) {
            ok = ::BCryptHashData(handle, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                  static_cast<ULONG>(data.size()), 0) >= 0 &&
                 ::BCryptFinishHash(handle, hash.data(), hash_size, 0) >= 0;
            ::BCryptDestroyHash(handle);
        }
    }
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) throw std::runtime_error("SHA-1 computation failed");

    static const char* hex = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < 8; ++i) {
        result.push_back(hex[hash[i] >> 4]);
        result.push_back(hex[hash[i] & 0x0F]);
    }
    return result;
}
#endif

} // namespace

std::string get_last_segment_url(const std::string& url) {
    const auto parts = parse_uri(url);
    if (!parts.has_value()) return url; // C# catch: return the input

    const auto& segments = last_segments(parts->path);
    if (segments.empty()) {
        // C#: segment == "/" or "//" → uri.Host with every "www." removed.
        return replace_all(parts->host, "www.", "");
    }
    return segments.back(); // C# also strips "/" from the segment
}

std::string get_stable_host_name(const std::string& file_path) {
    // Path.GetDirectoryName(filePath) ?? filePath
    const std::string folder = path::get_directory_name(file_path).value_or(file_path);

#ifdef _WIN32
    return sha1_first8_hex(folder);
#else
    (void)folder;
    throw std::runtime_error("GetStableHostName is only implemented on Windows");
#endif
}

std::string sanitize_url(const std::string& address) {
    if (is_whitespace_only(address)) {
        throw std::invalid_argument("address"); // C# ArgumentException
    }

    // Already absolute? Normalize. Otherwise assume https (UriBuilder with
    // Scheme = "https", Port = -1), which is what the C# does on UriFormatException.
    std::optional<UriParts> parsed = parse_uri(address);
    if (!parsed.has_value()) {
        parsed = parse_uri("https://" + address);
        // UriBuilder rejects a host that is empty or still carries separators
        // (this is why "/abs/path" fails rather than becoming a file URI).
        if (!parsed.has_value() || parsed->host.empty() ||
            parsed->host.find_first_of(" \t\\/?&#") != std::string::npos) {
            throw std::invalid_argument("address");
        }
    }
    const UriParts& parts = *parsed;

    std::string result = parts.scheme + "://";
    if (!parts.host.empty()) {
        result += parts.host;
        const bool default_port = (parts.scheme == "http" && parts.port == "80") ||
                                  (parts.scheme == "https" && parts.port == "443");
        if (!parts.port.empty() && !default_port) result += ":" + parts.port;
    }
    result += parts.path.empty() ? "/" : parts.path;
    result += parts.query;
    return result;
}

std::optional<std::string> try_sanitize_url(const std::string& address) {
    try {
        return sanitize_url(address);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void open_browser(const std::string& address) {
#ifdef _WIN32
    if (address.empty()) return;
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, address.c_str(),
                                             static_cast<int>(address.size()), nullptr, 0);
    if (needed <= 0) return;
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, address.c_str(), static_cast<int>(address.size()),
                          wide.data(), needed);
    ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)address;
#endif
}

#ifdef _WIN32
// Kept OUTSIDE the anonymous namespace: DisplayManager's synthetic device id
// calls this from another translation unit.
std::string sha256_hex(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    DWORD object_size = 0, hash_size = 0, produced = 0;
    std::vector<unsigned char> object, hash;
    bool ok = ::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                                  &produced, 0) >= 0 &&
              ::BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                  reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &produced,
                                  0) >= 0;
    if (ok) {
        object.resize(object_size);
        hash.resize(hash_size);
        BCRYPT_HASH_HANDLE handle = nullptr;
        ok = ::BCryptCreateHash(algorithm, &handle, object.data(), object_size, nullptr, 0, 0) >= 0;
        if (ok) {
            ok = ::BCryptHashData(handle, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                  static_cast<ULONG>(data.size()), 0) >= 0 &&
                 ::BCryptFinishHash(handle, hash.data(), hash_size, 0) >= 0;
            ::BCryptDestroyHash(handle);
        }
    }
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) throw std::runtime_error("SHA-256 computation failed");

    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(hash.size() * 2);
    for (unsigned char byte : hash) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}
#else
std::string sha256_hex(const std::string& data) {
    (void)data;
    throw std::runtime_error("sha256_hex requires Windows (BCrypt).");
}
#endif

} // namespace lively::common
