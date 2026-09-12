#pragma once
// Port of Lively.Models/Gallery/* — the REST DTOs of the wallpaper gallery API
// (Lively.Gallery.Client consumes these). JSON shape matches Newtonsoft
// defaults (PascalCase, nulls included) so the C++ client interoperates with
// the same backend.
//
// C# source files ported:
//   ApiResponse.cs, Page.cs, ProfileDto.cs, TokensModel.cs, WallpaperDto.cs,
//   SortingType.cs, VoteType.cs, ApiErrors.cs, HealthResult.cs,
//   HealthInfo.cs, ReportModel.cs

#include <lively/models/wallpaper_type.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lively::models::gallery {

using Json = nlohmann::ordered_json;

// ---- Enums (ordinal-identical to C#) ---------------------------------------

// SortingType.cs
enum class SortingType { all_time_top = 0, newest = 1, trending = 2 };

// VoteType.cs
enum class VoteType { downvote = 0, upvote = 1 };

// ReportModel.ReportType
enum class ReportType { other = 0, wrong_genre = 1, nudity_violence = 2, copyright_violation = 3, spam = 4 };

// ---- ApiErrors (static strings) --------------------------------------------

struct ApiErrors {
    static constexpr const char* NotFound = "NOT_FOUND";
    static constexpr const char* CodeCannotBeNullOrEmpty = "CODE_CANNOT_BE_NULL_OR_EMPTY";
    static constexpr const char* InvalidAccessToken = "INVALID_ACCESS_TOKEN";
    static constexpr const char* RefreshTokensDoesntMatch = "REFRESH_TOKENS_DOESNT_MATCH";
    static constexpr const char* InvalidSortingType = "INVALID_SORTING_TYPE";
    static constexpr const char* InvalidPageIndex = "INVALID_PAGE_INDEX";
    static constexpr const char* InvalidContentType = "INVALID_CONTENT_TYPE";
    static constexpr const char* MalformedArchive = "MALFORMED_WALLPAPER_ARCHIVE";
    static constexpr const char* AlreadySubscribedToWallpaper = "ALREADY_SUBSCRIBED_TO_WALLPAPER";
    static constexpr const char* NotSubscribedToWallpaper = "NOT_SUBSCRIBED_TO_WALLPAPER";
    static constexpr const char* TokensExpired = "TOKENS_EXPIRED";
};

// ---- DTOs -------------------------------------------------------------------

// ProfileDto.cs
struct ProfileDto {
    std::optional<std::string> id;
    std::optional<std::string> display_name;   // C# DisplayName
    std::optional<std::string> avatar_url;     // C# AvatarUrl

    Json to_json() const;
    static ProfileDto from_json(const Json& j);
};

// TokensModel.cs — Expiration is a DateTime; the port keeps the ISO-8601 form
// *already in Newtonsoft's canonical rendering* so that serializing the token
// store produces the exact bytes the C# app writes into Tokens.dat (the
// `tokenstore.plaintext` oracle line pins both a real value and the default).
//
// `normalize_expiration` converts a server-supplied timestamp into that form —
// without it, a token response echoed back through the port would differ from
// C#'s round-trip through DateTime. Note the interesting default: `default(DateTime)`
// renders as "0001-01-01T00:00:00" — no zone suffix and no fraction, which is
// NOT the same string as "0001-01-01T00:00:00Z".
struct TokensModel {
    std::optional<std::string> access_token;   // AccessToken
    std::optional<std::string> refresh_token;  // RefreshToken
    std::optional<std::string> provider;
    std::string expiration_iso = "0001-01-01T00:00:00";  // Expiration (default(DateTime))

    Json to_json() const;
    static TokensModel from_json(const Json& j);
};

// Newtonsoft's canonical DateTime rendering (DateFormatHandling.IsoDateFormat with
// DateTimeZoneHandling.RoundtripKind, the JsonConvert defaults):
//   * a `Z` suffix is kept and the value stays UTC;
//   * a fractional part is trimmed of trailing zeros, and dropped entirely when
//     it is all zeros ("...45.7890Z" -> "...45.789Z", "...45.0000000Z" -> "...45Z");
//   * no suffix stays suffix-less (DateTimeKind.Unspecified);
//   * the kind is never invented — an unparseable or empty input becomes
//     default(DateTime), exactly what C# produces for a missing JSON value.
// Documented deviation: a numeric offset ("+02:00") is preserved verbatim
// instead of being converted to local time, because the C# result depends on the
// machine's time zone and so is not reproducible in a test oracle.
std::string normalize_expiration(const std::string& iso);

// WallpaperDto.cs
struct WallpaperDto {
    std::optional<std::string> id;
    std::optional<std::string> app_version;    // AppVersion
    std::optional<std::string> title;
    std::optional<ProfileDto> author;
    std::optional<std::string> license;
    std::optional<std::string> contact;
    bool is_preview_available = false;         // IsPreviewAvailable
    std::optional<std::string> thumbnail;      // assigned client-side in C#
    std::optional<std::string> preview;        // assigned client-side in C#
    std::optional<std::string> type;           // C# WallpaperType (string form)
    int vote_count = 0;                        // VoteCount
    std::optional<std::string> description;
    std::optional<std::vector<std::string>> tags;

    Json to_json() const;
    static WallpaperDto from_json(const Json& j);
};

// Page<T> specialized for WallpaperDto (the only instantiation the client uses).
struct WallpaperPage {
    int number = 0;
    bool next_page_available = false;          // NextPageAvailable
    std::vector<WallpaperDto> data;

    Json to_json() const;
    static WallpaperPage from_json(const Json& j);
};

// ApiResponse<T> — T realized for the concrete types the C# client instantiates.
template <typename T>
struct ApiResponse {
    bool success = false;
    std::optional<T> data;
    std::optional<std::vector<std::string>> errors;
    int status_code = 0;                       // assigned from HTTP response
};

// HealthInfo.cs / HealthResult.cs — Duration is TimeSpan; Newtonsoft writes it
// as "c" ISO duration ("00:00:00.1234567"); kept as raw string here.
struct HealthInfo {
    std::optional<std::string> key;
    std::optional<std::string> description;
    std::optional<std::string> duration;       // TimeSpan string
    std::optional<std::string> status;
    std::optional<std::string> error;

    static HealthInfo from_json(const Json& j);
};

struct HealthResult {
    std::optional<std::string> name;
    std::optional<std::string> status;
    std::optional<std::string> duration;       // TimeSpan string
    std::optional<std::vector<HealthInfo>> info;

    static HealthResult from_json(const Json& j);
};

// ReportModel.cs
struct ReportModel {
    ReportType report = ReportType::other;
    std::optional<std::string> message;

    Json to_json() const;
};

// ---- JSON binding (Newtonsoft PascalCase names) -----------------------------

inline Json ProfileDto::to_json() const {
    Json j = Json::object();
    j["Id"] = id ? Json(*id) : Json(nullptr);
    j["DisplayName"] = display_name ? Json(*display_name) : Json(nullptr);
    j["AvatarUrl"] = avatar_url ? Json(*avatar_url) : Json(nullptr);
    return j;
}

inline ProfileDto ProfileDto::from_json(const Json& j) {
    ProfileDto p;
    if (j.contains("Id") && !j.at("Id").is_null()) p.id = j.at("Id").get<std::string>();
    if (j.contains("DisplayName") && !j.at("DisplayName").is_null()) p.display_name = j.at("DisplayName").get<std::string>();
    if (j.contains("AvatarUrl") && !j.at("AvatarUrl").is_null()) p.avatar_url = j.at("AvatarUrl").get<std::string>();
    return p;
}

inline Json TokensModel::to_json() const {
    Json j = Json::object();
    j["AccessToken"] = access_token ? Json(*access_token) : Json(nullptr);
    j["RefreshToken"] = refresh_token ? Json(*refresh_token) : Json(nullptr);
    j["Provider"] = provider ? Json(*provider) : Json(nullptr);
    j["Expiration"] = expiration_iso;
    return j;
}

namespace detail {

inline bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// "2030-06-01"
inline bool looks_like_date(const std::string& s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-' &&
           all_digits(s.substr(0, 4)) && all_digits(s.substr(5, 2)) && all_digits(s.substr(8, 2));
}

// "12:30:45"
inline bool looks_like_time(const std::string& s) {
    return s.size() == 8 && s[2] == ':' && s[5] == ':' &&
           all_digits(s.substr(0, 2)) && all_digits(s.substr(3, 2)) && all_digits(s.substr(6, 2));
}

} // namespace detail

inline std::string normalize_expiration(const std::string& iso) {
    constexpr const char* kDefault = "0001-01-01T00:00:00";
    if (iso.empty()) return kDefault;

    std::string core = iso;
    std::string zone;
    if (core.back() == 'Z' || core.back() == 'z') {
        zone = "Z";
        core.pop_back();
    } else {
        // A numeric offset (…+02:00 / …-05:00) appears after the date part.
        const std::size_t t = core.find_first_of("Tt ");
        if (t != std::string::npos && core.find_first_of("+-", t) != std::string::npos) {
            return iso;  // documented deviation: left verbatim
        }
    }

    // Fraction: 7 digits max (DateTime ticks), trailing zeros dropped.
    std::string fraction;
    const std::size_t dot = core.find('.');
    if (dot != std::string::npos) {
        fraction = core.substr(dot + 1);
        core.erase(dot);
        if (fraction.size() > 7) fraction.erase(7);
        while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    }

    std::string date = core;
    std::string time = "00:00:00";
    const std::size_t t = core.find_first_of("Tt ");
    if (t != std::string::npos) {
        date = core.substr(0, t);
        time = core.substr(t + 1);
        if (time.size() == 5) time += ":00";  // "12:30" is legal ISO
    }

    if (!detail::looks_like_date(date) || !detail::looks_like_time(time)) return kDefault;

    std::string out = date + "T" + time;
    if (!fraction.empty()) out += "." + fraction;
    out += zone;
    return out;
}

inline TokensModel TokensModel::from_json(const Json& j) {
    TokensModel t;
    if (j.contains("AccessToken") && !j.at("AccessToken").is_null()) t.access_token = j.at("AccessToken").get<std::string>();
    if (j.contains("RefreshToken") && !j.at("RefreshToken").is_null()) t.refresh_token = j.at("RefreshToken").get<std::string>();
    if (j.contains("Provider") && !j.at("Provider").is_null()) t.provider = j.at("Provider").get<std::string>();
    // A DateTime member round-trips through Newtonsoft's canonical rendering, so
    // the server's timestamp is normalized here rather than echoed verbatim.
    if (j.contains("Expiration") && !j.at("Expiration").is_null()) {
        t.expiration_iso = normalize_expiration(j.at("Expiration").get<std::string>());
    }
    return t;
}

inline Json WallpaperDto::to_json() const {
    Json j = Json::object();
    j["Id"] = id ? Json(*id) : Json(nullptr);
    j["AppVersion"] = app_version ? Json(*app_version) : Json(nullptr);
    j["Title"] = title ? Json(*title) : Json(nullptr);
    j["Author"] = author ? author->to_json() : Json(nullptr);
    j["License"] = license ? Json(*license) : Json(nullptr);
    j["Contact"] = contact ? Json(*contact) : Json(nullptr);
    j["IsPreviewAvailable"] = is_preview_available;
    j["Thumbnail"] = thumbnail ? Json(*thumbnail) : Json(nullptr);
    j["Preview"] = preview ? Json(*preview) : Json(nullptr);
    j["Type"] = type ? Json(*type) : Json(nullptr);
    j["VoteCount"] = vote_count;
    j["Description"] = description ? Json(*description) : Json(nullptr);
    if (tags) {
        Json arr = Json::array();
        for (const auto& t : *tags) arr.push_back(t);
        j["Tags"] = arr;
    } else {
        j["Tags"] = nullptr;
    }
    return j;
}

inline WallpaperDto WallpaperDto::from_json(const Json& j) {
    WallpaperDto w;
    if (j.contains("Id") && !j.at("Id").is_null()) w.id = j.at("Id").get<std::string>();
    if (j.contains("AppVersion") && !j.at("AppVersion").is_null()) w.app_version = j.at("AppVersion").get<std::string>();
    if (j.contains("Title") && !j.at("Title").is_null()) w.title = j.at("Title").get<std::string>();
    if (j.contains("Author") && !j.at("Author").is_null()) w.author = ProfileDto::from_json(j.at("Author"));
    if (j.contains("License") && !j.at("License").is_null()) w.license = j.at("License").get<std::string>();
    if (j.contains("Contact") && !j.at("Contact").is_null()) w.contact = j.at("Contact").get<std::string>();
    if (j.contains("IsPreviewAvailable")) w.is_preview_available = j.at("IsPreviewAvailable").get<bool>();
    if (j.contains("Thumbnail") && !j.at("Thumbnail").is_null()) w.thumbnail = j.at("Thumbnail").get<std::string>();
    if (j.contains("Preview") && !j.at("Preview").is_null()) w.preview = j.at("Preview").get<std::string>();
    if (j.contains("Type") && !j.at("Type").is_null()) w.type = j.at("Type").get<std::string>();
    if (j.contains("VoteCount")) w.vote_count = j.at("VoteCount").get<int>();
    if (j.contains("Description") && !j.at("Description").is_null()) w.description = j.at("Description").get<std::string>();
    if (j.contains("Tags") && !j.at("Tags").is_null()) {
        std::vector<std::string> ts;
        for (const auto& t : j.at("Tags")) ts.push_back(t.get<std::string>());
        w.tags = ts;
    }
    return w;
}

inline Json WallpaperPage::to_json() const {
    Json j = Json::object();
    j["Number"] = number;
    j["NextPageAvailable"] = next_page_available;
    Json arr = Json::array();
    for (const auto& w : data) arr.push_back(w.to_json());
    j["Data"] = arr;
    return j;
}

inline WallpaperPage WallpaperPage::from_json(const Json& j) {
    WallpaperPage p;
    if (j.contains("Number")) p.number = j.at("Number").get<int>();
    if (j.contains("NextPageAvailable")) p.next_page_available = j.at("NextPageAvailable").get<bool>();
    if (j.contains("Data") && !j.at("Data").is_null()) {
        for (const auto& item : j.at("Data")) p.data.push_back(WallpaperDto::from_json(item));
    }
    return p;
}

inline HealthInfo HealthInfo::from_json(const Json& j) {
    HealthInfo h;
    if (j.contains("Key") && !j.at("Key").is_null()) h.key = j.at("Key").get<std::string>();
    if (j.contains("Description") && !j.at("Description").is_null()) h.description = j.at("Description").get<std::string>();
    if (j.contains("Duration") && !j.at("Duration").is_null()) h.duration = j.at("Duration").get<std::string>();
    if (j.contains("Status") && !j.at("Status").is_null()) h.status = j.at("Status").get<std::string>();
    if (j.contains("Error") && !j.at("Error").is_null()) h.error = j.at("Error").get<std::string>();
    return h;
}

inline HealthResult HealthResult::from_json(const Json& j) {
    HealthResult r;
    if (j.contains("Name") && !j.at("Name").is_null()) r.name = j.at("Name").get<std::string>();
    if (j.contains("Status") && !j.at("Status").is_null()) r.status = j.at("Status").get<std::string>();
    if (j.contains("Duration") && !j.at("Duration").is_null()) r.duration = j.at("Duration").get<std::string>();
    if (j.contains("Info") && !j.at("Info").is_null()) {
        std::vector<HealthInfo> infos;
        for (const auto& item : j.at("Info")) infos.push_back(HealthInfo::from_json(item));
        r.info = infos;
    }
    return r;
}

inline Json ReportModel::to_json() const {
    Json j = Json::object();
    j["Report"] = static_cast<int>(report);  // Newtonsoft: enum as number
    j["Message"] = message ? Json(*message) : Json(nullptr);
    return j;
}

} // namespace lively::models::gallery
