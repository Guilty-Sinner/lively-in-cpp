#pragma once
// Port of Lively.Models/Services/AppUpdaterEventArgs.cs (enum + event args)
// and Lively.Common.Services/GithubUpdaterService.cs — the GitHub release
// update checker.
//
// C# semantics preserved:
//   * Status starts at notchecked; a failed fetch sets error.
//   * CompareAssemblyVersion: >0 update available, <0 beta/invalid (the
//     published setup version lower than local), ==0 up-to-date.
//   * Setup asset name: lively_setup_{arch}_full_v*.exe with x64 fallback to
//     the legacy x86 name.
//   * Retry timer: every 5 min tick, actual check only after 12h (30min after
//     an error) since LastCheckTime. C# System.Timers.Timer → jthread +
//     condition_variable wait (RAII stop).
//   * Debug builds never check (C# IsDebugBuild gate) — the C++ build maps
//     this to NDEBUG/LIVELY_ENABLE_UPDATER; tests inject the transport.

#include <lively/events.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lively::services {

// AppUpdateStatus (ordinal-identical).
enum class AppUpdateStatus { uptodate = 0, available = 1, invalid = 2, notchecked = 3, error = 4 };

// AppUpdaterEventArgs.
struct AppUpdaterEventArgs {
    AppUpdateStatus status = AppUpdateStatus::notchecked;
    std::string version;          // "1.2.3.4"
    std::chrono::system_clock::time_point check_time;
    std::optional<std::string> uri;
    std::optional<std::string> file_name;
};

// A GitHub release, shaped like Octokit's Release (only the used members).
struct GithubRelease {
    std::string tag_name;
    struct Asset {
        std::string name;
        std::string browser_download_url;
    };
    std::vector<Asset> assets;
};

// Transport abstraction: fetching releases is injectable so tests run offline
// and the UI tranche can wire a mock. Mirrors the GithubUtil static surface.
class IGithubFetcher {
public:
    virtual ~IGithubFetcher() = default;
    // Throws std::runtime_error on failure (C# exception propagation).
    virtual GithubRelease get_latest_release(const std::string& user_name,
                                             const std::string& repository_name) = 0;
};

// GithubUtil helpers, free functions (shared with the UI tranche).
namespace github_util {
// C# GetVersion: Regex.Replace(tagName, "[A-Za-z ]", "") → Version. Returns
// "0.0.0.0" when nothing remains.
std::string strip_tag_to_version(const std::string& tag_name);
// 4-component version compare (C# Version.CompareTo).
int compare_versions(const std::string& a, const std::string& b);
// C# GetAssetUrl: first asset whose name contains assetName (OrdinalIgnoreCase).
std::optional<GithubRelease::Asset> find_asset(const GithubRelease& release,
                                               const std::string& asset_name);
} // namespace github_util

class GithubUpdaterService {
public:
    explicit GithubUpdaterService(std::shared_ptr<IGithubFetcher> fetcher);
    ~GithubUpdaterService();

    AppUpdateStatus status() const;
    std::chrono::system_clock::time_point last_check_time() const;
    std::string last_check_version() const;
    std::optional<std::string> last_check_uri() const;
    std::optional<std::string> last_check_file_name() const;

    lively::event<AppUpdaterEventArgs> update_checked;

    void start();
    void stop();

    // C# CheckUpdate(fetchDelay): waits `fetch_delay` ms first.
    AppUpdateStatus check_update(int fetch_delay_ms = 0);

    // C# GetLatestRelease(isBeta): resolves the setup asset + version.
    struct LatestRelease {
        std::optional<std::string> uri;
        std::optional<std::string> file_name;
        std::string version;
    };
    LatestRelease get_latest_release(bool is_beta);

private:
    void timer_loop();
    static const char* arch_setup_string();

    std::shared_ptr<IGithubFetcher> fetcher_;

    mutable std::mutex mutex_;
    AppUpdateStatus status_ = AppUpdateStatus::notchecked;
    std::chrono::system_clock::time_point last_check_time_ = std::chrono::system_clock::time_point{};
    std::string last_check_version_ = "0.0.0.0";
    std::optional<std::string> last_check_uri_;
    std::optional<std::string> last_check_file_name_;

    std::jthread timer_thread_;
    std::condition_variable cv_;
    std::mutex timer_mutex_;
    bool running_ = false;
};

} // namespace lively::services
