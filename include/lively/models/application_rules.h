#pragma once
// Port of Lively.Models/ApplicationRulesModel.cs and AppMusicExclusionRuleModel.cs
// — the two per-application rule lists the core persists:
//
//   AppRules.json                JsonStorage<List<ApplicationRulesModel>>
//   MusicAppExclusionRules.json  JsonStorage<List<AppMusicExclusionRuleModel>>
//
// Both are `ObservableObject` + [ObservableProperty] upstream (INotifyPropertyChanged
// for the settings UI); the port keeps the state and drops the notifications, as
// with LibraryModel. Both are plain two-member shapes, pinned by the `persist`
// oracle: `Rule` is written as its enum ordinal (pause=0, ignore=1, kill=2) and a
// null AppPath stays null rather than becoming an empty string.

#include <lively/models/settings_model.h> // AppRules

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace lively::models {

// ApplicationRulesModel.cs
struct ApplicationRulesModel {
    std::string app_name;
    AppRules rule = AppRules::ignore;

    nlohmann::ordered_json to_json() const;
    static ApplicationRulesModel from_json(const nlohmann::ordered_json& json);
};

// AppMusicExclusionRuleModel.cs
struct AppMusicExclusionRuleModel {
    std::string app_name;
    std::optional<std::string> app_path; // C# null stays null

    nlohmann::ordered_json to_json() const;
    static AppMusicExclusionRuleModel from_json(const nlohmann::ordered_json& json);
};

// ---------------------------------------------------------------------------
// Inline definitions (small value types, like the other models headers).

inline nlohmann::ordered_json ApplicationRulesModel::to_json() const {
    nlohmann::ordered_json j;
    j["AppName"] = app_name;
    j["Rule"] = static_cast<int>(rule);
    return j;
}

inline ApplicationRulesModel ApplicationRulesModel::from_json(const nlohmann::ordered_json& json) {
    ApplicationRulesModel model;
    if (!json.is_object()) return model;
    model.app_name = json.value("AppName", std::string());
    if (json.contains("Rule") && json.at("Rule").is_number_integer()) {
        model.rule = static_cast<AppRules>(json.at("Rule").get<int>());
    }
    return model;
}

inline nlohmann::ordered_json AppMusicExclusionRuleModel::to_json() const {
    nlohmann::ordered_json j;
    j["AppName"] = app_name;
    j["AppPath"] = app_path.has_value() ? nlohmann::ordered_json(*app_path)
                                        : nlohmann::ordered_json(nullptr);
    return j;
}

inline AppMusicExclusionRuleModel AppMusicExclusionRuleModel::from_json(
    const nlohmann::ordered_json& json) {
    AppMusicExclusionRuleModel model;
    if (!json.is_object()) return model;
    model.app_name = json.value("AppName", std::string());
    if (json.contains("AppPath") && !json.at("AppPath").is_null()) {
        model.app_path = json.at("AppPath").get<std::string>();
    }
    return model;
}

} // namespace lively::models
