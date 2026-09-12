// Port of Lively.Utility.ConsoleDemo/Program.cs — interactive menu over the
// gRPC client layer. Phase-2 stub: the gRPC clients are not ported yet, so the
// menu prints the resolved intent (same navigation flow, same prompts).
// Wire-in point: replace the stubbed calls with WinDesktopCoreClient/
// CommandsClient/UserSettingsClient once the gRPC layer lands.

#include <iostream>
#include <string>

namespace {
void Menu() {
    std::cout << "\nChoose an option:\n"
                 "1) Set Wallpaper\n"
                 "2) Get Wallpapers\n"
                 "3) Get Screens\n"
                 "4) Close Wallpaper(s)\n"
                 "5) Get Settings(s)\n"
                 "6) Set Settings(s)\n"
                 "7) Start screensaver(s)\n"
                 "8) Commandline control\n"
                 "9) Exit\n"
                 "\r\nSelect an option: ";
}
} // namespace

int main() {
    std::cout << "lively console demo (C++ port; core link pending phase 2)\n";
    bool show_menu = true;
    while (show_menu) {
        Menu();
        std::string line;
        if (!std::getline(std::cin, line)) break;

        if (line == "1") {
            std::cout << "\nEnter display id:\n";
            std::string display_id;
            std::getline(std::cin, display_id);
            std::cout << "\nEnter wallpaper metadata path:\n";
            std::string path;
            std::getline(std::cin, path);
            std::cout << "[phase2] SetWallpaper(\"" << path << "\", \"" << display_id << "\")\n";
        } else if (line == "2") {
            std::cout << "[phase2] coreClient.Wallpapers\n";
        } else if (line == "3") {
            std::cout << "[phase2] displayManager.DisplayMonitors\n";
        } else if (line == "4") {
            std::cout << "\nChoose an option:\n"
                         "1) Close all wallpaper(s)\n"
                         "2) Close wallpaper - monitor\n"
                         "3) Close wallpaper - library\n"
                         "4) Close wallpaper - category\n"
                         "5) Exit\n"
                         "\r\nSelect an option: ";
            std::string sub;
            std::getline(std::cin, sub);
            if (sub == "1") std::cout << "[phase2] CloseAllWallpapers()\n";
            else if (sub != "5") std::cout << "NotImplementedException\n";
        } else if (line == "5") {
            std::cout << "[phase2] settingsClient.Settings\n";
        } else if (line == "6") {
            std::cout << "[phase2] Settings.SysTrayIcon = !SysTrayIcon; SaveAsync\n";
        } else if (line == "7") {
            std::cout << "Please wait..";
            std::cout << "\n[phase2] ShowScreensaver(false)\n";
        } else if (line == "8") {
            std::cout << "Enter commandline command:";
            std::string msg;
            std::getline(std::cin, msg);
            std::cout << "[phase2] AutomationCommand(\"" << msg << "\")\n";
        } else if (line == "9") {
            std::cout << "[phase2] ShutDown()\n";
            std::cout << "Core shut down complete..\n";
            std::getline(std::cin, line);
            show_menu = false;
        }
    }
    return 0;
}
