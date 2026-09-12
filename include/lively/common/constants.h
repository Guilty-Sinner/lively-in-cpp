#pragma once
// Port of Lively.Common/Constants.cs.
//
// C# exposes static string properties computed from
// Environment.SpecialFolder.LocalApplicationData; the C++ port exposes
// functions with identical return values (computed once, thread-safely).
// Path separators use std::filesystem::path semantics (\\ on Windows), equal
// to C# Path.Combine results.

#include <string>

namespace lively::common {

// Constants.ApplicationType
namespace application_type {
inline constexpr const wchar_t* kName = L"Lively Wallpaper";
inline constexpr bool kIsTestBuild = false;
} // namespace application_type

// Constants.SingleInstance
namespace single_instance {
inline constexpr const wchar_t* kUniqueAppName = L"LIVELY:DESKTOPWALLPAPERSYSTEM";
// PipeServerName = UniqueAppName + Environment.UserName (backward compat < v1.9)
std::wstring PipeServerName();
// GrpcPipeServerName = "Grpc_" + PipeServerName
std::wstring GrpcPipeServerName();
} // namespace single_instance

// Constants.CommonPaths
namespace common_paths {
std::wstring AppDataDir();     // %LOCALAPPDATA%\Lively Wallpaper
std::wstring LogDir();         // AppDataDir\logs
std::wstring LogDirUI();       // AppDataDir\UI
std::wstring TempDir();        // AppDataDir\temp
std::wstring TempCefDir();     // AppDataDir\Cef
std::wstring TempWebView2Dir();// AppDataDir\WebView2
std::wstring TempVideoDir();   // AppDataDir\Mpv
std::wstring AppRulesPath();   // AppDataDir\AppRules.json
std::wstring WallpaperLayoutPath();
std::wstring ScreenSaverLayoutPath();
std::wstring UserSettingsPath();
std::wstring MusicAppExclusionRulesPath();
std::wstring WeatherSettingsPath();
std::wstring ThemeDir();
std::wstring ThemeCacheDir();  // %TEMP%\Lively Wallpaper\themes
std::wstring CefRootCacheDir();// %TEMP%\Lively Wallpaper\CEF
std::wstring TokensPath();
std::wstring ScreenshotDir();
} // namespace common_paths

// Constants.CommonPartialPaths (combined with Settings.WallpaperDir)
namespace common_partial_paths {
inline constexpr const wchar_t* kWallpaperInstallDir = L"wallpapers";
inline constexpr const wchar_t* kWallpaperInstallTempDir = L"SaveData\\wptmp";
inline constexpr const wchar_t* kWallpaperSettingsDir = L"SaveData\\wpdata";
} // namespace common_partial_paths

// Constants.PlayerPartialPaths (combined with the core base directory)
namespace player_partial_paths {
inline constexpr const wchar_t* kMpvDir = L"plugins\\mpv";
inline constexpr const wchar_t* kMpvPath = L"plugins\\mpv\\mpv.exe";
inline constexpr const wchar_t* kCefSharpDir = L"plugins\\cef";
inline constexpr const wchar_t* kCefSharpPath = L"plugins\\cef\\Lively.Player.CefSharp.exe";
inline constexpr const wchar_t* kWebView2Dir = L"plugins\\webview2";
inline constexpr const wchar_t* kWebView2Path = L"plugins\\webview2\\Lively.Player.WebView2.exe";
inline constexpr const wchar_t* kWmfDir = L"plugins\\wmf";
inline constexpr const wchar_t* kWmfPath = L"plugins\\wmf\\Lively.PlayerWmf.exe";
inline constexpr const wchar_t* kVlcDir = L"plugins\\vlc";
inline constexpr const wchar_t* kVlcPath = L"plugins\\vlc\\vlc.exe";
inline constexpr const wchar_t* kLibVlcDir = L"plugins\\libvlc";
inline constexpr const wchar_t* kLibVlcPath = L"plugins\\libvlc\\Lively.Player.Vlc.exe";
} // namespace player_partial_paths

// Constants.MachineLearning
namespace machine_learning {
std::wstring BaseDir();  // AppDataDir\ML
std::wstring MiDaSDir(); // BaseDir\Midas
} // namespace machine_learning

// Narrow-string helper: %LOCALAPPDATA% (FOLDERID_LocalAppData) — used where C#
// calls Environment.GetFolderPath(SpecialFolder.LocalApplicationData) directly
// (e.g. SettingsModel default WallpaperDir).
std::string UserLocalAppDataDir();

// Narrow UTF-8 view of common_paths::TempVideoDir() for the filesystem helpers
// that operate on std::string (the media-wallpaper property fallback in
// WallpaperLibraryFactory).
std::string TempVideoDirNarrow();

} // namespace lively::common
