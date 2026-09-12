#pragma once
// Port of Lively.Models/DisplayMonitor.cs (display description shared by the
// gRPC contract, the settings model and the wallpaper/screensaver layout files).
// C# uses System.Drawing.Rectangle and IntPtr HMonitor; C++ keeps the same field
// names and value types.
//
// The JSON contract below is NOT the obvious one — it is what Newtonsoft emits
// for this class, pinned by the oracle (`layout` mode in tools/csharp_probe):
//
//   * `isStale` — a public *field*, so Newtonsoft writes it, and fields come
//     before properties: it is the FIRST member of the object;
//   * `Bounds`/`WorkingArea` — System.Drawing.Rectangle has a TypeConverter, so
//     they are written as the string "x, y, width, height", not as objects;
//   * `HMonitor` — IntPtr is written as an object: {"value": <int>}.
//
// Getting this wrong is not cosmetic: settings.json, WallpaperLayout.json and
// ScreenSaverLayout.json all nest displays, so a different shape makes files the
// C# app cannot read (and vice versa).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace lively::models {

struct Rectangle {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct DisplayMonitor {
    bool is_stale = false; // C# public field (not a property) — see above
    std::string device_id;    // C# DeviceId
    std::string device_name;  // C# DeviceName
    std::string display_name; // C# DisplayName
    std::int64_t h_monitor = 0; // C# IntPtr HMonitor (protobuf int32 HMonitor)
    bool is_primary = false;
    int index = 0;
    Rectangle bounds;
    Rectangle working_area;
};

// C# RectangleConverter: "x, y, width, height".
std::string rectangle_to_string(const Rectangle& r);
Rectangle rectangle_from_string(const std::string& text);

// Newtonsoft's DisplayMonitor contract, member order included.
nlohmann::ordered_json display_to_json(const DisplayMonitor& display);

// Tolerant on input: accepts the C# string form for the rectangles and the
// {"value": n} form for HMonitor, plus the object/number forms the port itself
// wrote before this contract was pinned.
DisplayMonitor display_from_json(const nlohmann::ordered_json& json);

// DisplayMonitor.Equals — IEquatable compares DeviceId only.
bool same_display(const DisplayMonitor& a, const DisplayMonitor& b);

} // namespace lively::models
