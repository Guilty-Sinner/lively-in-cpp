#include <lively/models/display_monitor.h>

#include <string>

namespace lively::models {

namespace {

using json = nlohmann::ordered_json;

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

Rectangle rect_from_object(const json& j) {
    Rectangle r;
    r.x = j.value("x", j.value("X", 0));
    r.y = j.value("y", j.value("Y", 0));
    r.width = j.value("width", j.value("Width", 0));
    r.height = j.value("height", j.value("Height", 0));
    return r;
}

} // namespace

std::string rectangle_to_string(const Rectangle& r) {
    return std::to_string(r.x) + ", " + std::to_string(r.y) + ", " + std::to_string(r.width) +
           ", " + std::to_string(r.height);
}

Rectangle rectangle_from_string(const std::string& text) {
    Rectangle r;
    int values[4] = {0, 0, 0, 0};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t comma = text.find(',', start);
        const std::string part =
            trim(text.substr(start, comma == std::string::npos ? comma : comma - start));
        try {
            values[i] = std::stoi(part);
        } catch (const std::exception&) {
            values[i] = 0;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    r.x = values[0];
    r.y = values[1];
    r.width = values[2];
    r.height = values[3];
    return r;
}

json display_to_json(const DisplayMonitor& display) {
    json j;
    // Declaration order: the public field first, then the properties.
    j["isStale"] = display.is_stale;
    j["Bounds"] = rectangle_to_string(display.bounds);
    j["DeviceId"] = display.device_id;
    j["DeviceName"] = display.device_name;
    j["DisplayName"] = display.display_name;
    j["HMonitor"]["value"] = display.h_monitor;
    j["Index"] = display.index;
    j["IsPrimary"] = display.is_primary;
    j["WorkingArea"] = rectangle_to_string(display.working_area);
    return j;
}

DisplayMonitor display_from_json(const json& j) {
    DisplayMonitor display;
    if (!j.is_object()) return display;

    if (j.contains("isStale") && j.at("isStale").is_boolean()) {
        display.is_stale = j.at("isStale").get<bool>();
    }
    if (j.contains("Bounds")) {
        const auto& bounds = j.at("Bounds");
        display.bounds = bounds.is_string() ? rectangle_from_string(bounds.get<std::string>())
                                            : rect_from_object(bounds);
    }
    display.device_id = j.value("DeviceId", std::string());
    display.device_name = j.value("DeviceName", std::string());
    display.display_name = j.value("DisplayName", std::string());
    if (j.contains("HMonitor")) {
        const auto& monitor = j.at("HMonitor");
        if (monitor.is_number_integer()) {
            display.h_monitor = monitor.get<std::int64_t>();
        } else if (monitor.is_object() && monitor.contains("value")) {
            display.h_monitor = monitor.at("value").get<std::int64_t>();
        }
    }
    display.index = j.value("Index", 0);
    display.is_primary = j.value("IsPrimary", false);
    if (j.contains("WorkingArea")) {
        const auto& area = j.at("WorkingArea");
        display.working_area = area.is_string() ? rectangle_from_string(area.get<std::string>())
                                                : rect_from_object(area);
    }
    return display;
}

bool same_display(const DisplayMonitor& a, const DisplayMonitor& b) {
    return a.device_id == b.device_id;
}

} // namespace lively::models
