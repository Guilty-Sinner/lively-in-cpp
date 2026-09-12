#include <lively/common/window_class_exclusions.h>

#include <algorithm>
#include <cctype>

namespace lively::common {

const std::vector<std::string>& desktop_classes() {
    static const std::vector<std::string> classes = {
        // Desktop
        "WorkerW",
        "Progman",
        // Start menu, taskview (win10), action center etc
        "Windows.UI.Core.CoreWindow",
        // Alt+tab screen (win10)
        "MultitaskingViewFrame",
        // Taskview (win11)
        "XamlExplorerHostIslandWindow",
        // Widget window (win11)
        "WindowsDashboard",
        // Taskbar(s)
        "Shell_TrayWnd",
        "Shell_SecondaryTrayWnd",
        // Systray notifyicon expanded popup
        "NotifyIconOverflowWindow",
        // Rainmeter widgets
        "RainmeterMeterWindow",
        // Coodesker, ref: https://github.com/rocksdanister/lively/issues/760
        "_cls_desk_",
    };
    return classes;
}

bool is_desktop_class(const std::string& window_class) {
    auto lower = [](const std::string& s) {
        std::string out = s;
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string needle = lower(window_class);
    const auto& classes = desktop_classes();
    return std::any_of(classes.begin(), classes.end(),
                       [&](const std::string& c) { return lower(c) == needle; });
}

} // namespace lively::common
