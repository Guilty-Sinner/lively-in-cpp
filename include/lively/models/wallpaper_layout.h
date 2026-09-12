#pragma once
// Port of Lively.Models/WallpaperLayoutModel.cs and ScreenSaverLayoutModel.cs —
// the two layout files the core keeps next to settings.json:
//
//   WallpaperLayout.json    JsonStorage<List<WallpaperLayoutModel>>
//   ScreenSaverLayout.json  JsonStorage<List<ScreenSaverLayoutModel>>
//
// Both are a JSON *array* at the root, written with the same indented/CRLF
// contract as livelyinfo.json, and both nest DisplayMonitor — so the display
// member contract in display_monitor.h is load-bearing here.
//
// `LivelyScreen` is the C# property name for Display ([JsonProperty] retained
// for backward compatibility with files written before v1.9): the field in the
// class is `Display` but the wire name is `LivelyScreen`, and changing it would
// orphan every existing user's layout.

#include <lively/models/display_monitor.h>
#include <lively/models/settings_model.h> // WallpaperArrangement

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lively::models {

struct WallpaperLayoutModel {
    std::shared_ptr<DisplayMonitor> display;      // C# Display → wire "LivelyScreen"
    std::optional<std::string> lively_info_path;  // C# null possible

    WallpaperLayoutModel() = default;
    WallpaperLayoutModel(std::shared_ptr<DisplayMonitor> screen, std::optional<std::string> path)
        : display(std::move(screen)), lively_info_path(std::move(path)) {}

    nlohmann::ordered_json to_json() const;
    static WallpaperLayoutModel from_json(const nlohmann::ordered_json& json);
};

struct ScreenSaverLayoutModel {
    WallpaperArrangement layout = WallpaperArrangement::per;
    // C# List can be null (the serializer writes `null`, not `[]`).
    std::optional<std::vector<WallpaperLayoutModel>> wallpapers;

    nlohmann::ordered_json to_json() const;
    static ScreenSaverLayoutModel from_json(const nlohmann::ordered_json& json);
};

} // namespace lively::models
