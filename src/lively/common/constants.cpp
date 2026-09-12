#include "lively/common/constants.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace lively::common {

namespace {

std::wstring env_local_appdata() {
    wchar_t* raw = nullptr;
    if (_wdupenv_s(&raw, nullptr, L"LOCALAPPDATA") == 0 && raw) {
        std::wstring value(raw);
        std::free(raw);
        return value;
    }
    // Fallback: SHGetKnownFolderPath (matches Environment.SpecialFolder.
    // LocalApplicationData).
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        std::wstring value(known);
        CoTaskMemFree(known);
        return value;
    }
    return L".";
}

std::wstring env_user_name() {
    wchar_t* raw = nullptr;
    if (_wdupenv_s(&raw, nullptr, L"USERNAME") == 0 && raw) {
        std::wstring value(raw);
        std::free(raw);
        return value;
    }
    return L"";
}

// Memoized Path.Combine equivalent: joins with the filesystem preferred
// separator, matching C# Path.Combine output.
std::wstring combine(const std::wstring& a, const std::wstring& b) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    return (fs::path(a) / fs::path(b)).wstring();
}

template <typename Fn>
std::wstring memo(Fn&& fn) {
    static std::once_flag flag;
    static std::wstring value;
    std::call_once(flag, [&] { value = fn(); });
    return value;
}

} // namespace

namespace single_instance {

std::wstring PipeServerName() {
    return memo([] {
        return std::wstring(kUniqueAppName) + env_user_name();
    });
}

std::wstring GrpcPipeServerName() {
    return memo([] { return L"Grpc_" + PipeServerName(); });
}

} // namespace single_instance

namespace common_paths {

std::wstring AppDataDir() {
    return memo([] { return combine(env_local_appdata(), application_type::kName); });
}
std::wstring LogDir() { return memo([] { return combine(AppDataDir(), L"logs"); }); }
std::wstring LogDirUI() { return memo([] { return combine(AppDataDir(), L"UI"); }); }
std::wstring TempDir() { return memo([] { return combine(AppDataDir(), L"temp"); }); }
std::wstring TempCefDir() { return memo([] { return combine(AppDataDir(), L"Cef"); }); }
std::wstring TempWebView2Dir() { return memo([] { return combine(AppDataDir(), L"WebView2"); }); }
std::wstring TempVideoDir() { return memo([] { return combine(AppDataDir(), L"Mpv"); }); }
std::wstring AppRulesPath() { return memo([] { return combine(AppDataDir(), L"AppRules.json"); }); }
std::wstring WallpaperLayoutPath() {
    return memo([] { return combine(AppDataDir(), L"WallpaperLayout.json"); });
}
std::wstring ScreenSaverLayoutPath() {
    return memo([] { return combine(AppDataDir(), L"ScreenSaverLayout.json"); });
}
std::wstring UserSettingsPath() {
    return memo([] { return combine(AppDataDir(), L"Settings.json"); });
}
std::wstring MusicAppExclusionRulesPath() {
    return memo([] { return combine(AppDataDir(), L"MusicAppExclusionRules.json"); });
}
std::wstring WeatherSettingsPath() {
    return memo([] { return combine(AppDataDir(), L"WeatherSettings.json"); });
}
std::wstring ThemeDir() { return memo([] { return combine(AppDataDir(), L"Themes"); }); }
std::wstring ThemeCacheDir() {
    return memo([] {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        return combine(combine(combine(temp, L"Lively Wallpaper"), L"themes"), L"");
    });
}
std::wstring CefRootCacheDir() {
    return memo([] {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        return combine(combine(combine(temp, L"Lively Wallpaper"), L"CEF"), L"");
    });
}
std::wstring TokensPath() { return memo([] { return combine(AppDataDir(), L"Tokens.dat"); }); }
std::wstring ScreenshotDir() {
    return memo([] { return combine(AppDataDir(), L"Screenshots"); });
}

} // namespace common_paths

namespace machine_learning {

std::wstring BaseDir() { return memo([] { return combine(common_paths::AppDataDir(), L"ML"); }); }
std::wstring MiDaSDir() { return memo([] { return combine(BaseDir(), L"Midas"); }); }

} // namespace machine_learning

std::string UserLocalAppDataDir() {
    // Same Known-Folder resolution as AppDataDir's fallback, returned narrow.
    wchar_t* known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        std::wstring ws(known);
        CoTaskMemFree(known);
        int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), size, nullptr, nullptr);
        return out;
    }
    return {};
}

} // namespace lively::common
