#pragma once
// Port of Lively.Models/LivelyInfoModel.cs and LivelyInfoLocalizationFile.cs —
// `livelyinfo.json`, the per-wallpaper metadata file (and its localization
// companion `LivelyInfo.loc.json`).
//
// C# property declaration order is the Newtonsoft emission order:
//   AppVersion, Title, Thumbnail, Preview, Desc, Author, License, Contact,
//   Type, FileName, Arguments, IsAbsolutePath, Id, Tags, Version
// so the port keeps that order when building the JSON document.
//
// Nullability: C# `string` properties are nullable and Newtonsoft writes `null`
// for them (JsonStorage uses NullValueHandling.Include), so every nullable
// field is std::optional<std::string>. `Thumbnail = ""` and `Thumbnail = null`
// are different on the wire and must stay different.

#include <lively/models/wallpaper_type.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace lively::models {

// C# `System.Reflection.Assembly.GetExecutingAssembly().GetName().Version`.
// This is a runtime reflection read with no compile-time counterpart: the value
// is the Lively.Models assembly version, which the project does not set
// explicitly, so it is the SDK default. The oracle probe emits the live value
// (`livelyinfo.appversion` in tests/goldens/library_csharp.txt) — if upstream
// ever adds an explicit <Version>, that line changes and this test fails.
inline constexpr const char* kLivelyInfoAppVersion = "1.0.0.0";

struct LivelyInfoModel {
    std::string app_version = kLivelyInfoAppVersion;
    std::optional<std::string> title;
    std::optional<std::string> thumbnail;
    std::optional<std::string> preview; // preview clip
    std::optional<std::string> desc;
    std::optional<std::string> author;
    std::optional<std::string> license;
    std::optional<std::string> contact;
    WallpaperType type = WallpaperType::web;
    std::optional<std::string> file_name;
    std::optional<std::string> arguments; // start commandline args
    bool is_absolute_path = false;
    std::optional<std::string> id; // gallery wallpaper id
    std::optional<std::vector<std::string>> tags;
    int version = 0;

    // JsonStorage<LivelyInfoModel>.StoreData bytes (indented, CRLF, nulls kept).
    std::string to_json_string() const;

    // JsonStorage<LivelyInfoModel>.LoadData: a missing/unreadable file or
    // malformed JSON throws, exactly like the C# path (the C# caller catches and
    // treats it as "corrupted wallpaper metadata").
    static LivelyInfoModel from_json_string(const std::string& json);

    nlohmann::ordered_json to_json() const;

    // C# `new LivelyInfoModel(info)`: copies every field but re-reads AppVersion
    // from the executing assembly, so the copy always carries the Lively.Models
    // version rather than the source's.
    static LivelyInfoModel copy(const LivelyInfoModel& source);
};

// C# LivelyInfoLocalizationFile — a Dictionary<string, LivelyInfoModel>.
struct LivelyInfoLocalizationFile {
    std::vector<std::pair<std::string, LivelyInfoModel>> languages;

    static LivelyInfoLocalizationFile from_json_string(const std::string& json);

    // C# TryGetValue semantics: ordinal (case-SENSITIVE) key lookup.
    const LivelyInfoModel* find(const std::string& code) const;
};

} // namespace lively::models
