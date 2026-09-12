#include <lively/common/lively_info_util.h>

#include <lively/common/languages.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lively::common {

namespace {

std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

std::optional<models::LivelyInfoModel> get_localized_info(const std::string& loc_path,
                                                         const std::string& language_code) {
    std::error_code ec;
    if (!std::filesystem::exists(loc_path, ec)) return std::nullopt;

    models::LivelyInfoLocalizationFile loc;
    try {
        loc = models::LivelyInfoLocalizationFile::from_json_string(read_all(loc_path));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (loc.languages.empty()) return std::nullopt; // C# `loc?.Languages is null`

    // ApplicationLanguages.PrimaryLanguageOverride is empty when not set.
    const std::string code = language_code.empty() ? ui_language() : language_code;
    if (code.empty()) return std::nullopt;

    if (const auto* exact = loc.find(code)) return *exact;

    // Base-language fallback: "zh-CN" → "zh" (case-sensitive, like the C#
    // Dictionary lookup, so "ZH-CN" does not match "zh-CN").
    const std::size_t dash = code.find('-');
    if (dash != std::string::npos) {
        const std::string base = code.substr(0, dash);
        if (const auto* fallback = loc.find(base)) return *fallback;
    }
    return std::nullopt;
}

} // namespace lively::common
