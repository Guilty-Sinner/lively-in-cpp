#include <lively/common/path_util.h>

#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lively::common::path {

namespace {

constexpr char kSeparator = '\\';

bool is_separator(char c) { return c == '/' || c == '\\'; }

bool is_rooted(const std::string& p) {
    if (p.empty()) return false;
    if (is_separator(p[0])) return true; // "\\x" / "/x"
    // "X:" or "X:\..." — a drive-qualified path is rooted on Windows.
    return p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0]));
}

std::string trim_trailing_separators(const std::string& p) {
    std::size_t end = p.size();
    while (end > 1 && is_separator(p[end - 1])) --end;
    return p.substr(0, end);
}

} // namespace

std::string combine(const std::string& first, const std::string& second) {
    if (first.empty()) return second;
    if (second.empty() || is_rooted(second)) return second;
    std::string result = first;
    if (!is_separator(result.back())) result += kSeparator;
    return result + second;
}

std::optional<std::string> combine_optional(const std::string& first,
                                            const std::optional<std::string>& second) {
    if (!second.has_value()) return std::nullopt; // ArgumentNullException → catch → null
    return combine(first, *second);
}

std::string get_extension(const std::string& p) {
    const std::string name = get_file_name(p);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == name.size()) return {};
    return name.substr(dot);
}

bool has_extension(const std::string& p) { return !get_extension(p).empty(); }

std::string get_file_name(const std::string& p) {
    const std::size_t sep = p.find_last_of("/\\");
    if (sep == std::string::npos) return p;
    return p.substr(sep + 1);
}

std::string get_file_name_without_extension(const std::string& p) {
    const std::string name = get_file_name(p);
    const std::string ext = get_extension(p);
    if (ext.empty()) return name;
    return name.substr(0, name.size() - ext.size());
}

std::optional<std::string> get_directory_name(const std::string& p) {
    if (p.empty()) return std::nullopt;
    const std::string trimmed = trim_trailing_separators(p);
    if (trimmed.empty()) return std::nullopt;

    const std::size_t sep = trimmed.find_last_of("/\\");
    if (sep == std::string::npos) return std::nullopt;

    std::string directory = trimmed.substr(0, sep);
    if (directory.empty()) return std::string(1, trimmed[0]); // "/x" → "/"
    // "C:\x" → "C:\" (the root keeps its separator), "C:\dir\x" → "C:\dir".
    if (directory.size() == 2 && directory[1] == ':') directory.push_back(trimmed[sep]);
    return directory;
}

std::string utf8_from_wide(const std::wstring& wide) {
    if (wide.empty()) return {};
#ifdef _WIN32
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
#else
    return std::string(wide.begin(), wide.end());
#endif
}

} // namespace lively::common::path
