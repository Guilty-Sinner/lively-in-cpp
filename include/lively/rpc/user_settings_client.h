#pragma once
// Port of Lively.Grpc.Client/IUserSettingsClient + UserSettingsClient.
//
// C# surface (IUserSettingsClient):
//   SettingsModel Settings { get; }   List<ApplicationRulesModel> AppRules { get; }
//   void Load<T>() / Task LoadAsync<T>()      // SettingsModel | List<ApplicationRulesModel>
//   void Save<T>() / Task SaveAsync<T>()
//
// The C# generic T-dispatch becomes two typed method pairs; the C#
// InvalidCastException branch for unknown T cannot occur with a typed API.
//
// Equivalence notes:
//  * Constructor blocks until BOTH Load<SettingsModel> and
//    Load<List<ApplicationRulesModel>> complete (C# Task.Run(...).Wait()).
//  * The grpc field mapping reproduces CreateGrpcSettings /
//    CreateSettingsFromGrpc EXACTLY — including fields the C# mapping skips
//    (GenerateTile, WaterMarkTile, IgnoreUpdateTag, DisplayIdentification,
//    LiveTile, ScreensaverGracePeriod, ScreensaverLockWaitTimeout,
//    TaskbarCrashTimeOutDelay, ProcessMonitorGridTile*) and the upstream
//    ScreensaverIdleTime minutes-vs-position enum cast (kept, with comment).
//
// Reference C#: Lively.Grpc.Client/UserSettingsClient.cs

#include <lively/models/settings_model.h>
#include <lively/task.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lively::rpc {

// Lively.Models/ApplicationRulesModel.cs
struct ApplicationRulesModel {
    std::string app_name;
    models::AppRules rule = models::AppRules::ignore;
};

class UserSettingsClient {
public:
    explicit UserSettingsClient(const std::string& target);
    ~UserSettingsClient();

    UserSettingsClient(const UserSettingsClient&) = delete;
    UserSettingsClient& operator=(const UserSettingsClient&) = delete;

    const std::optional<models::SettingsModel>& settings() const { return settings_; }
    const std::vector<ApplicationRulesModel>& app_rules() const { return app_rules_; }

    // Mutable accessors (C# exposes Settings/AppRules as settable properties;
    // callers mutate then Save<T>()).
    std::optional<models::SettingsModel>& settings() { return settings_; }
    std::vector<ApplicationRulesModel>& app_rules() { return app_rules_; }

    // C# Load<T> (sync stub) / LoadAsync<T> (await stub).
    void load_settings();
    void load_app_rules();
    Task<> load_settings_async();
    Task<> load_app_rules_async();

    // C# Save<T> — the C# sync variants use the blocking stub and discard the
    // result; async variants await the response.
    void save_settings();
    void save_app_rules();
    Task<> save_settings_async();
    Task<> save_app_rules_async();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::optional<models::SettingsModel> settings_;
    std::vector<ApplicationRulesModel> app_rules_;
};

} // namespace lively::rpc
