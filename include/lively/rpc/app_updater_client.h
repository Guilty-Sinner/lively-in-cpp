#pragma once
// Port of Lively.Grpc.Client/IAppUpdaterClient + AppUpdaterClient.
//
// C# surface (IAppUpdaterClient):
//   Status / LastCheckChangelog / LastCheckTime / LastCheckUri /
//   LastCheckFileName / LastCheckVersion
//   Task CheckUpdate(); Task StartUpdate();
//   Task<(Uri, string FileName, Version)> GetLatestRelease(bool isBeta);
//   Task SwitchReleaseChannel(bool isBeta, CancellationToken ct);
//   event EventHandler<AppUpdaterEventArgs> UpdateChecked;
//
// C# construction: blocking GetUpdateStatus refresh, then a
// SubscribeUpdateChecked stream loop; every stream ping re-refreshes the
// status (GetUpdateStatus RPC) and raises UpdateChecked — ported identically.

#include <lively/events.h>
#include <lively/task.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lively::rpc {

// Lively.Models/Services/AppUpdaterEventArgs.cs
enum class AppUpdateStatus : int {
    uptodate = 0,
    available,
    invalid,
    notchecked,
    error,
};

// C# System.Version — dot-separated components; absent parts are nullopt.
struct AppVersion {
    int major = 0;
    std::optional<int> minor;
    std::optional<int> build;
    std::optional<int> revision;

    static std::optional<AppVersion> parse(const std::string& s); // C# new Version(str)
};

// EventArgs payload (C# AppUpdaterEventArgs).
struct AppUpdaterEventArgs {
    AppUpdateStatus update_status;
    std::optional<AppVersion> update_version;
    std::int64_t update_date_epoch = 0; // C# DateTime (local); seconds since epoch
    std::string update_uri;             // C# Uri; empty == null
    std::string file_name;              // empty == null
};

// C# (Uri Url, string FileName, Version AppVersion) tuple.
struct LatestRelease {
    std::string url;   // empty == C# null
    std::string file_name;
    std::optional<AppVersion> app_version;
};

class AppUpdaterClient {
public:
    explicit AppUpdaterClient(const std::string& target);
    ~AppUpdaterClient(); // RAII: cancel + join the subscription thread

    AppUpdaterClient(const AppUpdaterClient&) = delete;
    AppUpdaterClient& operator=(const AppUpdaterClient&) = delete;

    event<AppUpdaterEventArgs> update_checked;

    AppUpdateStatus status() const { return status_; }
    std::int64_t last_check_time_epoch() const { return last_check_time_epoch_; }
    std::optional<AppVersion> last_check_version() const { return last_check_version_; }
    const std::string& last_check_changelog() const { return last_check_changelog_; }
    const std::string& last_check_uri() const { return last_check_uri_; }
    const std::string& last_check_file_name() const { return last_check_file_name_; }

    Task<> check_update();
    Task<> start_update();
    Task<LatestRelease> get_latest_release(bool is_beta);
    Task<> switch_release_channel(bool is_beta);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void refresh_status(); // C# UpdateStatusRefresh (blocking helper)
    void subscribe_update_checked_loop();

    AppUpdateStatus status_ = AppUpdateStatus::notchecked;
    std::int64_t last_check_time_epoch_ = 0; // C# DateTime.MinValue sentinel ≈ epoch 0
    std::optional<AppVersion> last_check_version_;
    std::string last_check_changelog_;
    std::string last_check_uri_;
    std::string last_check_file_name_;
};

} // namespace lively::rpc
