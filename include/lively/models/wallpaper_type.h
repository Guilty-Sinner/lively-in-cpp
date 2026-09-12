#pragma once
// Port of Lively.Models/Enums/WallpaperType.cs — ordinal == proto
// WallpaperCategory values (verified: app=0 .. picture=11, identical order).
// C# casts (WallpaperCategory)(int)WallpaperType directly.

namespace lively::models {

enum class WallpaperType : int {
    app = 0,
    web,
    webaudio,
    url,
    bizhawk,
    unity,
    godot,
    video,
    gif,
    unityaudio,
    videostream,
    picture,
};

// Port of Lively.Models/Enums/WebPageType.cs — passed to the web players as
// `--wallpaper-type <name>` (parsed case-sensitively by CommandLineParser).
enum class WebPageType : int {
    online = 0,
    local,
};

} // namespace lively::models
