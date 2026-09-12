#include <lively/models/wallpaper_layout.h>

namespace lively::models {

namespace {

using json = nlohmann::ordered_json;

json wallpaper_array_to_json(const std::vector<WallpaperLayoutModel>& wallpapers) {
    json array = json::array();
    for (const auto& wallpaper : wallpapers) array.push_back(wallpaper.to_json());
    return array;
}

std::vector<WallpaperLayoutModel> wallpaper_array_from_json(const json& array) {
    std::vector<WallpaperLayoutModel> wallpapers;
    if (!array.is_array()) return wallpapers;
    for (const auto& item : array) wallpapers.push_back(WallpaperLayoutModel::from_json(item));
    return wallpapers;
}

} // namespace

json WallpaperLayoutModel::to_json() const {
    json j;
    j["LivelyScreen"] = display ? display_to_json(*display) : json(nullptr);
    j["LivelyInfoPath"] =
        lively_info_path.has_value() ? json(*lively_info_path) : json(nullptr);
    return j;
}

WallpaperLayoutModel WallpaperLayoutModel::from_json(const json& j) {
    WallpaperLayoutModel layout;
    if (!j.is_object()) return layout;
    if (j.contains("LivelyScreen") && !j.at("LivelyScreen").is_null()) {
        layout.display = std::make_shared<DisplayMonitor>(display_from_json(j.at("LivelyScreen")));
    }
    if (j.contains("LivelyInfoPath") && !j.at("LivelyInfoPath").is_null()) {
        layout.lively_info_path = j.at("LivelyInfoPath").get<std::string>();
    }
    return layout;
}

json ScreenSaverLayoutModel::to_json() const {
    json j;
    j["Layout"] = static_cast<int>(layout);
    j["Wallpapers"] = wallpapers.has_value() ? wallpaper_array_to_json(*wallpapers)
                                             : json(nullptr);
    return j;
}

ScreenSaverLayoutModel ScreenSaverLayoutModel::from_json(const json& j) {
    ScreenSaverLayoutModel layout;
    if (!j.is_object()) return layout;
    if (j.contains("Layout") && j.at("Layout").is_number_integer()) {
        layout.layout = static_cast<WallpaperArrangement>(j.at("Layout").get<int>());
    }
    if (j.contains("Wallpapers") && !j.at("Wallpapers").is_null()) {
        layout.wallpapers = wallpaper_array_from_json(j.at("Wallpapers"));
    }
    return layout;
}

} // namespace lively::models
