#pragma once
// Port of Lively.Models/DisplayMonitor.cs (display description shared by the
// gRPC contract and the settings model). C# uses System.Drawing.Rectangle and
// IntPtr HMonitor; C++ keeps the same field names and value types.
//
// Reference C#: Lively.Models/DisplayMonitor.cs

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
    std::string device_id;    // C# DeviceId
    std::string device_name;  // C# DeviceName
    std::string display_name; // C# DisplayName
    std::int64_t h_monitor = 0; // C# IntPtr HMonitor (protobuf int32 HMonitor)
    bool is_primary = false;
    int index = 0;
    Rectangle bounds;
    Rectangle working_area;
};

} // namespace lively::models
