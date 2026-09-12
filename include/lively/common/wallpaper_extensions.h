#pragma once
// Port of Lively.Common/Extensions/WallpaperExtensions.cs.
//
// C# extension methods on WallpaperType become free functions (C++ has no
// extension methods). The exhaustive test in tests/test_library.cpp evaluates
// every predicate for every enumerator against the C# oracle, which is what
// keeps a reordered or extended enum from silently changing classification.

#include <lively/models/wallpaper_type.h>

namespace lively::common {

inline bool is_online_wallpaper(models::WallpaperType type) {
    return type == models::WallpaperType::url || type == models::WallpaperType::videostream;
}

inline bool is_web_wallpaper(models::WallpaperType type) {
    return type == models::WallpaperType::web || type == models::WallpaperType::webaudio ||
           type == models::WallpaperType::url;
}

inline bool is_video_wallpaper(models::WallpaperType type) {
    return type == models::WallpaperType::video || type == models::WallpaperType::videostream;
}

inline bool is_application_wallpaper(models::WallpaperType type) {
    return type == models::WallpaperType::unity || type == models::WallpaperType::unityaudio ||
           type == models::WallpaperType::app || type == models::WallpaperType::godot ||
           type == models::WallpaperType::bizhawk;
}

// "Picture, gif and other non dynamic format"
inline bool is_media_wallpaper(models::WallpaperType type) {
    return is_video_wallpaper(type) || type == models::WallpaperType::gif ||
           type == models::WallpaperType::picture;
}

inline bool is_local_web_wallpaper(models::WallpaperType type) {
    return is_web_wallpaper(type) && !is_online_wallpaper(type);
}

// "Determines if the wallpaper type uses a directory-based structure, meaning it
// may include multiple files and subdirectories."
inline bool is_directory_project(models::WallpaperType type) {
    return is_application_wallpaper(type) || is_local_web_wallpaper(type);
}

inline bool is_device_input_allowed(models::WallpaperType type) {
    switch (type) {
    case models::WallpaperType::app:
    case models::WallpaperType::web:
    case models::WallpaperType::webaudio:
    case models::WallpaperType::url:
    case models::WallpaperType::bizhawk:
    case models::WallpaperType::unity:
    case models::WallpaperType::godot:
    case models::WallpaperType::unityaudio:
        return true;
    case models::WallpaperType::video:
    case models::WallpaperType::gif:
    case models::WallpaperType::videostream:
    case models::WallpaperType::picture:
        return false;
    }
    return false; // C# `_ => false`
}

} // namespace lively::common
