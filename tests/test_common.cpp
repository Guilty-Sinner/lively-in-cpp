#include <catch2/catch_test_macros.hpp>

#include <lively/common/app_lifecycle.h>
#include <lively/common/constants.h>
#include <lively/common/log_util.h>
#include <lively/common/system_info.h>

#include <windows.h>

#include <filesystem>
#include <string>

using namespace lively::common;

namespace {
bool ends_with(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

TEST_CASE("constants match C# Constants.cs values", "[common]") {
    namespace fs = std::filesystem;
    const std::wstring app_data = common_paths::AppDataDir();
    REQUIRE(ends_with(app_data, L"Lively Wallpaper"));

    REQUIRE(ends_with(common_paths::LogDir(), L"logs"));
    REQUIRE(ends_with(common_paths::TempCefDir(), L"Cef"));
    REQUIRE(ends_with(common_paths::UserSettingsPath(), L"Settings.json"));
    REQUIRE(ends_with(common_paths::AppRulesPath(), L"AppRules.json"));
    REQUIRE(ends_with(common_paths::WallpaperLayoutPath(), L"WallpaperLayout.json"));
    REQUIRE(ends_with(common_paths::ScreenshotDir(), L"Screenshots"));
    REQUIRE(ends_with(common_paths::TokensPath(), L"Tokens.dat"));

    // Single-instance names: pipe names derive from the mutex name + user.
    REQUIRE(std::wstring(single_instance::kUniqueAppName) == L"LIVELY:DESKTOPWALLPAPERSYSTEM");
    const std::wstring pipe = single_instance::PipeServerName();
    REQUIRE(pipe.rfind(L"LIVELY:DESKTOPWALLPAPERSYSTEM", 0) == 0);
    REQUIRE(single_instance::GrpcPipeServerName().rfind(L"Grpc_", 0) == 0);

    // Player relative paths (verbatim from C#).
    REQUIRE(std::wstring(player_partial_paths::kMpvPath) == L"plugins\\mpv\\mpv.exe");
    REQUIRE(std::wstring(player_partial_paths::kCefSharpPath) ==
            L"plugins\\cef\\Lively.Player.CefSharp.exe");
    REQUIRE(std::wstring(player_partial_paths::kLibVlcPath) ==
            L"plugins\\libvlc\\Lively.Player.Vlc.exe");
}

TEST_CASE("SystemInfo output prefixes match C# format", "[common]") {
    // C# emits "GPU: ", "CPU: ", "OS: " prefixed, multi-line for multiple GPUs.
    const std::string gpu = SystemInfo::GetGpuInfo();
    REQUIRE(gpu.rfind("GPU: ", 0) == 0);
    REQUIRE_FALSE(SystemInfo::GetGpu().empty());

    const std::string cpu = SystemInfo::GetCpuInfo();
    REQUIRE(cpu.rfind("CPU: ", 0) == 0);

    const std::string os = SystemInfo::GetOSInfo();
    REQUIRE(os.rfind("OS: ", 0) == 0);
}

TEST_CASE("PropertyList failure message matches C# (sic)", "[common]") {
    class Bomb : public PropertyListable {
        std::string to_property_list() const override { throw std::runtime_error("boom"); }
    };
    REQUIRE(LogUtil::PropertyList(Bomb{}) == "Failed to retrive properties of object.");
}

TEST_CASE("PropertyList formats lines like StringBuilder.AppendLine", "[common]") {
    class Item : public PropertyListable {
        std::string to_property_list() const override {
            return "Name: abc\nValue: 42\n";
        }
    };
    REQUIRE(LogUtil::PropertyList(Item{}) == "Name: abc\nValue: 42\n");
}

TEST_CASE("app lifecycle detects nothing running in test environment", "[common]") {
    // The Lively core is not running here: mutex absent => nil version.
    REQUIRE_FALSE(AppLifeCycleUtil::IsAppMutexRunning(
        std::wstring(single_instance::kUniqueAppName) + L"__nonexistent__"));
    REQUIRE(AppLifeCycleUtil::GetRunningLivelyAppVer() == AppLifeCycleUtil::LivelyAppVer::nil);

    // A named pipe we create must be detectable, mirroring IsNamedPipeExists.
    const std::wstring pipe_name = L"lively_cpp_test_pipe_oracle";
    const std::wstring full = L"\\\\.\\pipe\\" + pipe_name;
    HANDLE server = CreateNamedPipeW(
        full.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
        1, 1024, 1024, 0, nullptr);
    REQUIRE(server != INVALID_HANDLE_VALUE);
    REQUIRE(AppLifeCycleUtil::IsNamedPipeExists(pipe_name));
    DisconnectNamedPipe(server);
    CloseHandle(server);
}

TEST_CASE("GetWin32Error format matches C# template", "[common]") {
    ::SetLastError(5);
    const std::string text = LogUtil::GetWin32Error("demo fail", "member", "file.cs", 12);
    REQUIRE(text == "HRESULT: 5, demo fail at\nfile.cs (12)\nmember");
}
