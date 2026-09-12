#pragma once
// Port of Lively.Models/ThemeModel.cs — the persisted descriptor of an installed
// application theme (`<ThemeDir>/<random>/theme.json`).
//
// Newtonsoft details that the wire format depends on:
//   * `AppVersion` is the first member and is NOT a constructor parameter — it
//     defaults to the entry assembly's version. The port stamps the C#-format
//     value the caller supplies, matching SettingsModel's convention (the oracle
//     fixture records the probe's own "0.0.0.0").
//   * `IsEditable` carries [JsonIgnore], so it is runtime-only state and must not
//     reach the file. This is the one member a hand-written port is likely to
//     serialize by accident.
//   * `Name` is a plain `string` property with no initializer, so it is NULLABLE:
//     a default-constructed ThemeModel writes `"Name": null`, not `"Name": ""`.
//     (Only AppVersion is non-null by default, because it has an initializer.)
//   * `Type` is a two-member enum, so it lands on the wire as its ordinal
//     (picture=0, shader=1).
//   * `Tags` is a nullable List<string> — a null stays `null`, not `[]` — and so
//     are Contact / License / AccentColor / Preview / File in C# (they are plain
//     `string` properties with no initializer).

#include <lively/common/json_format.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace lively::models {

// ThemeType.cs
enum class ThemeType : int { picture = 0, shader };

// ThemeModel.cs — `AppVersion` default mirrors SettingsModel's convention: the
// C++ port cannot read an assembly version, so it stamps the C#-format value the
// composition root supplies ("0.0.0.0" until the app sets it).
inline constexpr const char* kThemeDefaultAppVersion = "0.0.0.0";

struct ThemeModel {
    std::string app_version = kThemeDefaultAppVersion;
    std::optional<std::string> name;  // C# null by default, hence optional
    std::optional<std::string> description;
    std::optional<std::string> contact;
    std::optional<std::string> license;
    std::optional<std::string> file;
    std::optional<std::string> preview;
    std::optional<std::string> accent_color;   // AccentColor, hex
    ThemeType type = ThemeType::picture;
    std::optional<std::vector<std::string>> tags;

    // [JsonIgnore] — deliberately NOT serialized.
    bool is_editable = false;

    ThemeModel() = default;

    // C# `new ThemeModel(ThemeModel model)` — the copy constructor.
    //
    // It faithfully reproduces an upstream quirk that is observable in the
    // persisted file: the C# copy constructor assigns every member EXCEPT
    // AppVersion. So `new ThemeModel(theme)` re-runs AppVersion's field
    // initializer (the entry assembly's version) instead of carrying the
    // source's value over. AppThemeFactory goes through exactly that path, which
    // means an installed theme's theme.json records the *host app's* version
    // even when the source model had a different one.
    //
    // It matters in the port because the natural C++ instinct — a compiler-
    // generated copy, or `ThemeModel copy = source;` — would preserve AppVersion
    // and silently produce a different file. Hence a named factory rather than
    // an operator: the divergence should be visible at every call site.
    static ThemeModel copy_constructor(const ThemeModel& source);

    nlohmann::ordered_json to_json() const;
    static ThemeModel from_json(const nlohmann::ordered_json& json);

    // JsonStorage<ThemeModel>.StoreData == this. Indented/CRLF, no trailing newline.
    std::string to_json_string() const { return common::newtonsoft_indented(to_json()); }
};

inline ThemeModel ThemeModel::copy_constructor(const ThemeModel& source) {
    ThemeModel copy;
    copy.app_version = kThemeDefaultAppVersion;  // NOT copied — see the note above
    copy.name = source.name;
    copy.description = source.description;
    copy.contact = source.contact;
    copy.license = source.license;
    copy.file = source.file;
    copy.preview = source.preview;
    copy.accent_color = source.accent_color;
    copy.type = source.type;
    copy.tags = source.tags;
    // IsEditable is not assigned by the C# copy constructor either, so it
    // resets to false unless the caller's object initializer sets it.
    copy.is_editable = false;
    return copy;
}

inline nlohmann::ordered_json ThemeModel::to_json() const {
    nlohmann::ordered_json j;
    j["AppVersion"] = app_version;
    j["Name"] = name ? nlohmann::ordered_json(*name) : nlohmann::ordered_json(nullptr);
    j["Description"] = description ? nlohmann::ordered_json(*description)
                                   : nlohmann::ordered_json(nullptr);
    j["Contact"] = contact ? nlohmann::ordered_json(*contact) : nlohmann::ordered_json(nullptr);
    j["License"] = license ? nlohmann::ordered_json(*license) : nlohmann::ordered_json(nullptr);
    j["File"] = file ? nlohmann::ordered_json(*file) : nlohmann::ordered_json(nullptr);
    j["Preview"] = preview ? nlohmann::ordered_json(*preview) : nlohmann::ordered_json(nullptr);
    j["AccentColor"] = accent_color ? nlohmann::ordered_json(*accent_color)
                                    : nlohmann::ordered_json(nullptr);
    j["Type"] = static_cast<int>(type);
    if (tags) {
        nlohmann::ordered_json arr = nlohmann::ordered_json::array();
        for (const auto& tag : *tags) arr.push_back(tag);
        j["Tags"] = arr;
    } else {
        j["Tags"] = nullptr;
    }
    // is_editable intentionally omitted: [JsonIgnore].
    return j;
}

inline ThemeModel ThemeModel::from_json(const nlohmann::ordered_json& json) {
    ThemeModel model;
    if (!json.is_object()) return model;

    const auto read = [&json](const char* key) -> std::optional<std::string> {
        if (!json.contains(key) || json.at(key).is_null()) return std::nullopt;
        return json.at(key).get<std::string>();
    };

    if (json.contains("AppVersion") && !json.at("AppVersion").is_null()) {
        model.app_version = json.at("AppVersion").get<std::string>();
    }
    model.name = read("Name");
    model.description = read("Description");
    model.contact = read("Contact");
    model.license = read("License");
    model.file = read("File");
    model.preview = read("Preview");
    model.accent_color = read("AccentColor");
    if (json.contains("Type") && json.at("Type").is_number_integer()) {
        model.type = static_cast<ThemeType>(json.at("Type").get<int>());
    }
    if (json.contains("Tags") && json.at("Tags").is_array()) {
        std::vector<std::string> tags;
        for (const auto& tag : json.at("Tags")) tags.push_back(tag.get<std::string>());
        model.tags = tags;
    }
    // IsEditable is [JsonIgnore]: never read from the file.
    return model;
}

} // namespace lively::models
