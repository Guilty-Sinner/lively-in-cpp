// gRPC port verification.
//
// 1. In-process round trip: a C++ server implements the CommandsService with
//    Lively-equivalent behavior; the ported CommandsClient talks to it over a
//    real socket — every Task-returning method is exercised.
// 2. Cross-language oracle: when LIVELY_CSHARP_GRPC_HOST is set (see
//    tools/csharp_probe/grpc_server_readme.md), the same client runs against a
//    server built from Lively's *actual* C# gRPC service implementations,
//    proving the wire contract matches the original ecosystem.

#include <catch2/catch_test_macros.hpp>

#ifdef LIVELY_RPC_ENABLED

#include <lively/rpc/commands_client.h>
#include <lively/rpc/desktop_core_client.h>
#include <lively/rpc/display_manager_client.h>
#include <lively/rpc/app_updater_client.h>
#include <lively/rpc/user_settings_client.h>

#include <lively/common/exceptions.h>
#include <lively/models/ipc_message.h>
#include <lively/models/settings_model.h>

#include <grpcpp/grpcpp.h>

#include <commands.grpc.pb.h>
#include <display.grpc.pb.h>
#include <settings.grpc.pb.h>
#include <update.grpc.pb.h>
#include <wallpaper.grpc.pb.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace lively;
using Lively::Grpc::Common::Proto::Commands::AutomationCommandRequest;
using Lively::Grpc::Common::Proto::Commands::CommandsService;
using Lively::Grpc::Common::Proto::Commands::RestartRequest;
using Lively::Grpc::Common::Proto::Commands::ScreensaverRequest;
using Empty = google::protobuf::Empty;

namespace {

// Records every received request so tests can assert exact wire content.
// All CommandsService methods are unary → ServerUnaryReactor overrides.
class RecordingService final : public CommandsService::CallbackService {
public:
    grpc::ServerUnaryReactor* ShowUI(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("ShowUI");
        return Noop();
    }
    grpc::ServerUnaryReactor* CloseUI(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("CloseUI");
        return Noop();
    }
    grpc::ServerUnaryReactor* RestartUI(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("RestartUI");
        return Noop();
    }
    grpc::ServerUnaryReactor* RestartUIWithArgs(
        grpc::CallbackServerContext*, const RestartRequest* request, Empty*) override {
        seen.insert("RestartUIWithArgs:" + request->start_args());
        return Noop();
    }
    grpc::ServerUnaryReactor* ShowDebugger(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("ShowDebugger");
        return Noop();
    }
    grpc::ServerUnaryReactor* Screensaver(
        grpc::CallbackServerContext*, const ScreensaverRequest* request, Empty*) override {
        seen.insert("Screensaver:" + std::to_string(static_cast<int>(request->state())) +
                    ":" + std::to_string(request->preview_hwnd()) + ":" +
                    (request->fade_in() ? "1" : "0"));
        return Noop();
    }
    grpc::ServerUnaryReactor* ShutDown(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("ShutDown");
        return Noop();
    }
    grpc::ServerUnaryReactor* AutomationCommand(
        grpc::CallbackServerContext*, const AutomationCommandRequest* request, Empty*) override {
        std::string joined;
        for (const auto& a : request->args()) {
            if (!joined.empty()) joined += "|";
            joined += a;
        }
        seen.insert("AutomationCommand:" + joined);
        return Noop();
    }
    grpc::ServerUnaryReactor* SaveRectUI(
        grpc::CallbackServerContext*, const Empty*, Empty*) override {
        seen.insert("SaveRectUI");
        return Noop();
    }

    std::set<std::string> seen;

private:
    // gRPC 1.82+: OnDone() is pure virtual; the reactor owns itself until the
    // library calls OnDone, where we delete it (standard callback-API pattern).
    class NoopReactor final : public grpc::ServerUnaryReactor {
    public:
        NoopReactor() { Finish(grpc::Status::OK); }
        void OnDone() override { delete this; }
        void OnCancel() override {}
    };
    static grpc::ServerUnaryReactor* Noop() {
        return new NoopReactor();
    }
};

struct TestServer {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<RecordingService> service;
    std::string port;

    static TestServer start() {
        auto svc = std::make_unique<RecordingService>();
        int selected_port = 0;
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        builder.RegisterService(svc.get());
        TestServer ts;
        ts.service = std::move(svc);
        ts.server = builder.BuildAndStart();
        REQUIRE(ts.server != nullptr);
        ts.port = std::to_string(selected_port);
        return ts;
    }

private:
    static std::string& svc_port_storage() {
        static thread_local std::string p; // builder writes the selected port here
        p.clear();
        return p;
    }
};

} // namespace

TEST_CASE("CommandsClient round-trips every CommandsService method", "[rpc]") {
    auto ts = TestServer::start();
    rpc::CommandsClient client("127.0.0.1:" + ts.port);

    SECTION("empty-request methods") {
        client.ShowUI().get();
        client.CloseUI().get();
        client.RestartUI().get();
        client.ShowDebugger().get();
        client.ShutDown().get();
        client.SaveRectUIAsync().get();
        client.RestartUI("setwp --file C:\\x.jpg").get();
        client.ShowScreensaver(true).get();
        client.StopScreensaver().get();
        client.ScreensaverConfigure().get();
        client.ScreensaverPreview(42).get();
        client.AutomationCommandAsync({"setwp", "--file", "x"}).get();

        // Fire-and-forget sync variants (C# void methods).
        client.AutomationCommand({"app", "--showApp", "true"});
        client.SaveRectUI();

        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // let sync calls land

        auto& s = ts.service->seen;
        CHECK(s.count("ShowUI") == 1);
        CHECK(s.count("CloseUI") == 1);
        CHECK(s.count("RestartUI") == 1);
        CHECK(s.count("ShowDebugger") == 1);
        CHECK(s.count("ShutDown") == 1);
        CHECK(s.count("SaveRectUI") >= 1);
        CHECK(s.count("RestartUIWithArgs:setwp --file C:\\x.jpg") == 1);
        CHECK(s.count("Screensaver:0:0:1") == 1);           // start, no hwnd, fade_in
        CHECK(s.count("Screensaver:1:0:0") == 1);           // stop
        CHECK(s.count("Screensaver:3:0:0") == 1);           // configure
        CHECK(s.count("Screensaver:2:42:0") == 1);          // preview + hwnd
        CHECK(s.count("AutomationCommand:setwp|--file|x") == 1);
        CHECK(s.count("AutomationCommand:app|--showApp|true") >= 1);
    }
}

TEST_CASE("CommandsClient surfaces RPC failures as exceptions", "[rpc]") {
    // No server on this port → connection refused → failed status → the
    // coroutine task must rethrow (C# stub throws RpcException equivalently).
    rpc::CommandsClient client("127.0.0.1:1");
    REQUIRE_THROWS_AS(client.ShowUI().get(), std::runtime_error);
}

namespace {

// The same request program run against any CommandsService target — used to
// compare a C++ in-process server vs the real C# server (crosslang).
void run_request_program(rpc::CommandsClient& client) {
    client.ShowUI().get();
    client.CloseUI().get();
    client.RestartUI().get();
    client.RestartUI("setwp --file C:\\x.jpg").get();
    client.ShowDebugger().get();
    client.ShutDown().get();
    client.SaveRectUIAsync().get();
    client.ShowScreensaver(true).get();
    client.StopScreensaver().get();
    client.ScreensaverConfigure().get();
    client.ScreensaverPreview(42).get();
    client.AutomationCommandAsync({"setwp", "--file", "x"}).get();
}

// Server-side record of one test run, normalized into the canonical
// "METHOD:params" set shared by both server implementations.
std::set<std::string> load_golden_lines(const std::string& path) {
    std::set<std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("REC ", 0) == 0) out.insert(line.substr(4));
    }
    return out;
}

bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

} // namespace

// Cross-language oracle: run the identical request program against the ported
// C++ client+server AND against a real C# server (Lively.Grpc.Common bindings,
// driven by `tools/csharp_probe grpcserver`). Both servers write REC lines in
// the same canonical format; the sets must be identical — proving the proto
// contract port is wire-compatible with the original C# ecosystem.
// ---- DesktopService tests (server-streaming + event mapping) ----

namespace {

// gRPC 1.82 reactor API: server-streaming hooks return a ServerWriteReactor
// that drives itself (StartWrite → OnWriteDone → Finish). Writes queued before
// the stream is bound are buffered by the library.
template <typename R>
class CannedWriter final : public grpc::ServerWriteReactor<R> {
public:
    explicit CannedWriter(std::vector<R> items) : items_(std::move(items)) {
        NextWrite();
    }
    void OnWriteDone(bool ok) override {
        if (!ok) {
            this->Finish(grpc::Status(grpc::StatusCode::CANCELLED, "write failed"));
            return;
        }
        NextWrite();
    }
    void NextWrite() {
        if (next_ >= items_.size()) {
            this->Finish(grpc::Status::OK);
        } else {
            this->StartWrite(&items_[next_++]);
        }
    }
    void OnDone() override { delete this; }
    void OnCancel() override {}

private:
    std::vector<R> items_;
    std::size_t next_ = 0;
};

class UnaryNoopReactor final : public grpc::ServerUnaryReactor {
public:
    UnaryNoopReactor() { Finish(grpc::Status::OK); }
    void OnDone() override { delete this; }
    void OnCancel() override {}
};

using Lively::Grpc::Common::Proto::Desktop::CloseAllWallpapersRequest;
using Lively::Grpc::Common::Proto::Desktop::CloseWallpaperCategoryRequest;
using Lively::Grpc::Common::Proto::Desktop::CloseWallpaperLibraryRequest;
using Lively::Grpc::Common::Proto::Desktop::CloseWallpaperMonitorRequest;
using Lively::Grpc::Common::Proto::Desktop::CreateWallpaperRequest;
using Lively::Grpc::Common::Proto::Desktop::CreateWallpaperResponse;
using Lively::Grpc::Common::Proto::Desktop::DesktopService;
using Lively::Grpc::Common::Proto::Desktop::EditWallpaperRequest;
using Lively::Grpc::Common::Proto::Desktop::EditWallpaperResponse;
using Lively::Grpc::Common::Proto::Desktop::ErrorCategory;
using Lively::Grpc::Common::Proto::Desktop::GetCoreStatsResponse;
using Lively::Grpc::Common::Proto::Desktop::GetWallpapersResponse;
using Lively::Grpc::Common::Proto::Desktop::PreviewWallpaperRequest;
using Lively::Grpc::Common::Proto::Desktop::ScreenData;
using Lively::Grpc::Common::Proto::Desktop::SetWallpaperRequest;
using Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse;
using Lively::Grpc::Common::Proto::Desktop::WallpaperMessageRequest;
using Lively::Grpc::Common::Proto::Desktop::WallpaperScreenshotRequest;
using DEmpty = google::protobuf::Empty;

// Stub desktop-core server: emits canned wallpaper data, records requests,
// supports the three server-streams and error injection.
class FakeDesktopService final : public DesktopService::CallbackService {
public:
    grpc::ServerUnaryReactor* GetCoreStats(
        grpc::CallbackServerContext*, const DEmpty*, GetCoreStatsResponse* response) override {
        response->set_base_directory("C:/fake/lively");
        response->set_assembly_version("2.0.5.0");
        response->set_is_core_initialized(true);
        return noop();
    }

    grpc::ServerWriteReactor<GetWallpapersResponse>* GetWallpapers(
        grpc::CallbackServerContext*, const DEmpty*) override {
        seen.insert("GetWallpapers");
        return new CannedWriter<GetWallpapersResponse>(canned);
    }

    grpc::ServerUnaryReactor* SetWallpaper(
        grpc::CallbackServerContext*, const SetWallpaperRequest* request, DEmpty*) override {
        seen.insert("SetWallpaper:" + request->lively_info_path() + ":" + request->monitor_id() +
                    ":" + std::to_string(static_cast<int>(request->type())));
        return noop();
    }
    grpc::ServerUnaryReactor* EditWallpaper(
        grpc::CallbackServerContext*, const EditWallpaperRequest*, EditWallpaperResponse* response) override {
        response->set_is_success(true);
        if (edit_error_category >= 0) {
            response->mutable_error()->set_error(
                static_cast<ErrorCategory>(edit_error_category));
            response->mutable_error()->set_error_msg("boom");
        }
        return noop();
    }
    grpc::ServerUnaryReactor* PreviewWallpaper(
        grpc::CallbackServerContext*, const PreviewWallpaperRequest*, DEmpty*) override {
        seen.insert("PreviewWallpaper");
        return noop();
    }
    grpc::ServerUnaryReactor* CloseAllWallpapers(
        grpc::CallbackServerContext*, const CloseAllWallpapersRequest*, DEmpty*) override {
        seen.insert("CloseAllWallpapers");
        return noop();
    }
    grpc::ServerUnaryReactor* CloseWallpaperMonitor(
        grpc::CallbackServerContext*, const CloseWallpaperMonitorRequest* request, DEmpty*) override {
        seen.insert("CloseWallpaperMonitor:" + request->monitor_id());
        return noop();
    }
    grpc::ServerUnaryReactor* CloseWallpaperLibrary(
        grpc::CallbackServerContext*, const CloseWallpaperLibraryRequest* request, DEmpty*) override {
        seen.insert("CloseWallpaperLibrary:" + request->lively_info_path());
        return noop();
    }
    grpc::ServerUnaryReactor* CloseWallpaperCategory(
        grpc::CallbackServerContext*, const CloseWallpaperCategoryRequest* request, DEmpty*) override {
        seen.insert("CloseWallpaperCategory:" + std::to_string(static_cast<int>(request->category())));
        return noop();
    }
    grpc::ServerUnaryReactor* SendMessageWallpaper(
        grpc::CallbackServerContext*, const WallpaperMessageRequest* request, DEmpty*) override {
        seen.insert("SendMessageWallpaper:" + request->monitor_id() + ":" +
                    request->lively_info_path() + ":" + request->msg());
        return noop();
    }
    grpc::ServerUnaryReactor* TakeScreenshot(
        grpc::CallbackServerContext*, const WallpaperScreenshotRequest* request, DEmpty*) override {
        seen.insert("TakeScreenshot:" + request->monitor_id() + ":" + request->save_path());
        return noop();
    }

    // Server-streaming subscriptions: emit `n_changed` Empty msgs / `n_errors`
    // error msgs, then complete.
    grpc::ServerWriteReactor<DEmpty>* SubscribeWallpaperChanged(
        grpc::CallbackServerContext*, const DEmpty*) override {
        return new CannedWriter<DEmpty>(std::vector<DEmpty>(static_cast<std::size_t>(n_changed), DEmpty{}));
    }
    grpc::ServerWriteReactor<Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse>*
    SubscribeWallpaperError(
        grpc::CallbackServerContext*, const DEmpty*) override {
        std::vector<Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse> errors;
        for (int i = 0; i < n_errors; ++i) {
            Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse err;
            err.set_error(error_category_seq.empty()
                          ? ErrorCategory::general : error_category_seq.front());
            if (!error_category_seq.empty()) error_category_seq.pop_front();
            err.set_error_msg("error #" + std::to_string(i));
            errors.push_back(std::move(err));
        }
        return new CannedWriter<Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse>(
            std::move(errors));
    }
    grpc::ServerUnaryReactor* CreateWallpaper(
        grpc::CallbackServerContext*, const CreateWallpaperRequest*, CreateWallpaperResponse* response) override {
        response->set_is_success(true);
        response->set_lively_info_path("C:/fake/new_wp");
        return noop();
    }

    std::set<std::string> seen;
    std::vector<GetWallpapersResponse> canned;
    int n_changed = 0;
    int n_errors = 0;
    std::deque<ErrorCategory> error_category_seq;
    int edit_error_category = -1;

private:
    static grpc::ServerUnaryReactor* noop() {
        return new UnaryNoopReactor();
    }
};

} // namespace

TEST_CASE("DesktopCoreClient streams wallpapers and raises events", "[rpc][desktop]") {
    FakeDesktopService svc;
    // Canned wallpaper: a video wallpaper on the primary display.
    GetWallpapersResponse wp;
    wp.set_lively_info_path("C:/wps/video1");
    wp.set_property_copy_path("C:/wps/video1/LivelyProperties.json");
    wp.set_preview_path("C:/wps/video1/preview.jpg");
    wp.set_thumbnail_path("C:/wps/video1/thumbnail.jpg");
    wp.set_category(Lively::Grpc::Common::Proto::Desktop::WallpaperCategory::video);
    auto* screen = wp.mutable_screen();
    screen->set_device_id("\\\\?\\DISPLAY#1");
    screen->set_device_name("\\\\.\DISPLAY1");
    screen->set_display_name("Display 1");
    screen->set_h_monitor(65537);
    screen->set_index(0);
    screen->set_is_primary(true);
    screen->mutable_bounds()->set_x(0);
    screen->mutable_bounds()->set_width(1920);
    screen->mutable_bounds()->set_height(1080);
    screen->mutable_working_area()->set_y(48);
    screen->mutable_working_area()->set_width(1920);
    screen->mutable_working_area()->set_height(1032);
    svc.canned.push_back(wp);
    svc.n_changed = 1;
    svc.n_errors = 2;
    svc.error_category_seq.push_back(ErrorCategory::workerw);
    svc.error_category_seq.push_back(ErrorCategory::wallpaper_not_found);

    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);

    int changed_count = 0;
    std::mutex changed_mutex;
    std::deque<std::string> error_names;
    std::mutex error_mutex;

    {
        rpc::DesktopCoreClient client("127.0.0.1:" + std::to_string(port));

        client.wallpaper_changed.subscribe([&](const auto&) {
            std::lock_guard<std::mutex> lk(changed_mutex);
            ++changed_count;
        });
        client.wallpaper_error.subscribe([&](const std::exception_ptr& e) {
            std::string name;
            try { std::rethrow_exception(e); }
            catch (const lively::common::WorkerWException&) { name = "workerw"; }
            catch (const lively::common::WallpaperNotFoundException&) { name = "notfound"; }
            catch (const std::exception& ex) { name = std::string("other:") + ex.what(); }
            std::lock_guard<std::mutex> lk(error_mutex);
            error_names.push_back(name);
        });

        // Give the subscription threads a moment to drain the streams.
        for (int i = 0; i < 100 && changed_count == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        for (int i = 0; i < 100 && error_names.size() < 2; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        // Ctor-time snapshots.
        auto wps = client.wallpapers();
        REQUIRE(wps.size() == 1);
        CHECK(wps[0].lively_info_folder_path == "C:/wps/video1");
        CHECK(wps[0].category == lively::models::WallpaperType::video);
        CHECK(wps[0].display.device_id == "\\\\?\\DISPLAY#1");
        CHECK(wps[0].display.h_monitor == 65537);
        CHECK(wps[0].display.is_primary);
        CHECK(wps[0].display.bounds.width == 1920);
        CHECK(client.base_directory() == "C:/fake/lively");
        CHECK(client.assembly_version() == "2.0.5.0");
        CHECK(client.is_core_initialized());

        // Unary round-trips.
        client.set_wallpaper("C:/wps/video1", "\\\\?\\DISPLAY#1").get();
        client.close_all_wallpapers().get();
        client.close_wallpaper("\\\\?\\DISPLAY#1").get();
        client.close_wallpaper_library("C:/wps/video1").get();
        client.close_wallpaper_category(lively::models::WallpaperType::gif).get();
        client.preview_wallpaper("C:/wps/video1").get();
        client.take_screenshot("\\\\?\\DISPLAY#1", "C:/shot.jpg").get();
        auto edited = client.edit_wallpaper("C:/wps/video1").get();
        CHECK(edited);
        auto created = client.create_wallpaper("C:/in.mp4", lively::models::WallpaperType::video, "").get();
        CHECK(created == "C:/fake/new_wp");

        // IPC message through the wire (uses the ported Newtonsoft serializer).
        lively::models::LivelyVolumeCmd vol;
        vol.volume = 55;
        client.send_message_wallpaper("", "C:/wps/video1", vol);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } // dtor: cancel subscriptions + join (also proves RAII cleanup doesn't hang)

    auto& s = svc.seen;
    CHECK(s.count("SetWallpaper:C:/wps/video1:\\\\?\\DISPLAY#1:1") == 1); // type=ready
    CHECK(s.count("CloseAllWallpapers") == 1);
    CHECK(s.count("CloseWallpaperMonitor:\\\\?\\DISPLAY#1") == 1);
    CHECK(s.count("CloseWallpaperLibrary:C:/wps/video1") == 1);
    CHECK(s.count("CloseWallpaperCategory:8") == 1); // gif == 8
    CHECK(s.count("PreviewWallpaper") == 1);
    CHECK(s.count("TakeScreenshot:\\\\?\\DISPLAY#1:C:/shot.jpg") == 1);
    CHECK(s.count("SendMessageWallpaper::C:/wps/video1:{\"Type\":9,\"Volume\":55}") == 1);

    CHECK(changed_count >= 1);
    {
        std::lock_guard<std::mutex> lk(error_mutex);
        REQUIRE(error_names.size() == 2);
        CHECK(error_names[0] == "workerw");
        CHECK(error_names[1] == "notfound");
    }
}

// ---- DisplayService tests ----

namespace {

using Lively::Grpc::Common::Proto::Display::DisplayService;
using Lively::Grpc::Common::Proto::Display::GetScreensResponse;
using Lively::Grpc::Common::Proto::Display::Rectangle;
using REmpty = google::protobuf::Empty;

class FakeDisplayService final : public DisplayService::CallbackService {
public:
    grpc::ServerUnaryReactor* GetScreens(
        grpc::CallbackServerContext*, const REmpty*, GetScreensResponse* response) override {
        for (const auto& s : screens) *response->add_screens() = s;
        return noop();
    }
    grpc::ServerUnaryReactor* GetVirtualScreenBounds(
        grpc::CallbackServerContext*, const REmpty*, Rectangle* response) override {
        response->set_x(0);
        response->set_y(0);
        response->set_width(3840);
        response->set_height(1080);
        return noop();
    }
    grpc::ServerWriteReactor<REmpty>* SubscribeDisplayChanged(
        grpc::CallbackServerContext*, const REmpty*) override {
        return new CannedWriter<REmpty>(std::vector<REmpty>(n_pings, REmpty{}));
    }

    std::vector<Lively::Grpc::Common::Proto::Display::ScreenData> screens;
    int n_pings = 0;

private:
    static grpc::ServerUnaryReactor* noop() { return new UnaryNoopReactor(); }
};

Lively::Grpc::Common::Proto::Display::ScreenData make_screen(
    const char* id, int index, bool primary, int x, int width) {
    Lively::Grpc::Common::Proto::Display::ScreenData s;
    s.set_device_id(id);
    s.set_device_name(std::string("\\\\.\DISPLAY") + std::to_string(index + 1));
    s.set_display_name("Display " + std::to_string(index + 1));
    s.set_h_monitor(65537 + index);
    s.set_index(index);
    s.set_is_primary(primary);
    s.mutable_bounds()->set_x(x);
    s.mutable_bounds()->set_width(width);
    s.mutable_bounds()->set_height(1080);
    s.mutable_working_area()->set_x(x);
    s.mutable_working_area()->set_width(width);
    s.mutable_working_area()->set_height(1032);
    return s;
}

} // namespace

TEST_CASE("DisplayManagerClient snapshots displays and streams changes", "[rpc][display]") {
    FakeDisplayService svc;
    svc.screens.push_back(make_screen("\\\\?\\DISPLAY#1", 0, true, 0, 1920));
    svc.screens.push_back(make_screen("\\\\?\\DISPLAY#2", 1, false, 1920, 1920));
    svc.n_pings = 1;

    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);

    int changed_count = 0;
    {
        rpc::DisplayManagerClient client("127.0.0.1:" + std::to_string(port));
        client.display_changed.subscribe([&](const auto&) { ++changed_count; });

        // Ctor-time snapshots.
        auto monitors = client.display_monitors();
        REQUIRE(monitors.size() == 2);
        CHECK(monitors[0].device_id == "\\\\?\\DISPLAY#1");
        CHECK(monitors[0].bounds.width == 1920);
        CHECK(monitors[1].bounds.x == 1920);
        CHECK(client.virtual_screen_bounds().width == 3840);
        auto primary = client.primary_monitor();
        REQUIRE(primary.has_value());
        CHECK(primary->is_primary);
        CHECK(primary->device_id == "\\\\?\\DISPLAY#1");

        // Wait for the display-changed stream to be drained.
        for (int i = 0; i < 100 && changed_count == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(changed_count >= 1);
    } // dtor: cancel + join
}

// ---- UpdateService tests ----

namespace {

using Lively::Grpc::Common::Proto::Update::GetLatestReleaseRequest;
using Lively::Grpc::Common::Proto::Update::GetLatestReleaseResponse;
using Lively::Grpc::Common::Proto::Update::ReleaseChannel;
using Lively::Grpc::Common::Proto::Update::UpdateResponse;
using Lively::Grpc::Common::Proto::Update::UpdateService;
using UEmpty = google::protobuf::Empty;

class FakeUpdateService final : public UpdateService::CallbackService {
public:
    grpc::ServerUnaryReactor* GetUpdateStatus(
        grpc::CallbackServerContext*, const UEmpty*, UpdateResponse* response) override {
        response->set_status(status);
        response->set_version(version);
        response->set_url(url);
        response->set_changelog("fixes");
        response->set_file_name("setup.exe");
        response->mutable_time()->set_seconds(1700000000);
        ++status_calls;
        return noop();
    }
    grpc::ServerUnaryReactor* CheckUpdate(
        grpc::CallbackServerContext*, const UEmpty*, UEmpty*) override {
        seen.insert("CheckUpdate");
        return noop();
    }
    grpc::ServerUnaryReactor* StartUpdate(
        grpc::CallbackServerContext*, const UEmpty*, UEmpty*) override {
        seen.insert("StartUpdate");
        return noop();
    }
    grpc::ServerUnaryReactor* GetLatestRelease(
        grpc::CallbackServerContext*, const GetLatestReleaseRequest* request,
        GetLatestReleaseResponse* response) override {
        seen.insert(std::string("GetLatestRelease:") +
                    (request->channel() == ReleaseChannel::beta ? "beta" : "stable"));
        response->set_version("2.1.0.4");
        response->set_url("https://example.com/lively_setup.exe");
        response->set_file_name("lively_setup.exe");
        return noop();
    }
    grpc::ServerUnaryReactor* SwitchReleaseChannel(
        grpc::CallbackServerContext*, const Lively::Grpc::Common::Proto::Update::SwitchReleaseChannelRequest* request, UEmpty*) override {
        seen.insert(std::string("SwitchChannel:") +
                    (request->channel() == ReleaseChannel::beta ? "beta" : "stable"));
        return noop();
    }
    grpc::ServerWriteReactor<UEmpty>* SubscribeUpdateChecked(
        grpc::CallbackServerContext*, const UEmpty*) override {
        return new CannedWriter<UEmpty>(std::vector<UEmpty>(n_pings, UEmpty{}));
    }

    Lively::Grpc::Common::Proto::Update::UpdateStatus status =
        Lively::Grpc::Common::Proto::Update::UpdateStatus::uptodate;
    std::string version = "2.0.5.0";
    std::string url = "https://example.com/lively_setup.exe";
    int n_pings = 0;
    int status_calls = 0;
    std::set<std::string> seen;

private:
    static grpc::ServerUnaryReactor* noop() { return new UnaryNoopReactor(); }
};

} // namespace

TEST_CASE("AppUpdaterClient refreshes status and raises update events", "[rpc][update]") {
    FakeUpdateService svc;
    svc.status = Lively::Grpc::Common::Proto::Update::UpdateStatus::available;
    svc.n_pings = 2;

    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);

    int events = 0;
    {
        rpc::AppUpdaterClient client("127.0.0.1:" + std::to_string(port));
        client.update_checked.subscribe([&](const rpc::AppUpdaterEventArgs& e) {
            ++events;
            CHECK(e.update_status == rpc::AppUpdateStatus::available);
            REQUIRE(e.update_version.has_value());
            CHECK(e.update_version->major == 2);
            CHECK(e.update_version->minor == 0);
            CHECK(e.update_uri == "https://example.com/lively_setup.exe");
        });

        // Ctor refresh + 2 stream pings (each ping re-refreshes → 3 calls).
        for (int i = 0; i < 100 && events < 2; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(events >= 2);
        CHECK(client.status() == rpc::AppUpdateStatus::available);
        CHECK(client.last_check_version()->build == 5);
        CHECK(client.last_check_file_name() == "setup.exe");
        CHECK(client.last_check_time_epoch() == 1700000000);

        client.check_update().get();
        client.start_update().get();
        client.switch_release_channel(true).get();
        auto rel = client.get_latest_release(false).get();
        CHECK(rel.url == "https://example.com/lively_setup.exe");
        CHECK(rel.file_name == "lively_setup.exe");
        REQUIRE(rel.app_version.has_value());
        CHECK(rel.app_version->revision == 4);
    } // dtor: cancel + join

    CHECK(svc.seen.count("CheckUpdate") == 1);
    CHECK(svc.seen.count("StartUpdate") == 1);
    CHECK(svc.seen.count("SwitchChannel:beta") == 1);
    CHECK(svc.seen.count("GetLatestRelease:stable") == 1);
    CHECK(svc.status_calls >= 3); // ctor + one per ping
}

TEST_CASE("AppVersion::parse matches C# Version semantics", "[rpc][update]") {
    auto v4 = rpc::AppVersion::parse("2.1.0.4");
    REQUIRE(v4.has_value());
    CHECK(v4->major == 2); CHECK(v4->minor == 1); CHECK(v4->build == 0); CHECK(v4->revision == 4);
    auto v2 = rpc::AppVersion::parse("2.1");
    REQUIRE(v2.has_value());
    CHECK(v2->major == 2); CHECK(v2->minor == 1);
    CHECK(!v2->build.has_value());
    CHECK(!rpc::AppVersion::parse("").has_value()); // C# null/empty → null
    CHECK_THROWS_AS(rpc::AppVersion::parse("1.2.3.4.5"), std::runtime_error);
    CHECK_THROWS_AS(rpc::AppVersion::parse("1.x"), std::invalid_argument);
}

// ---- SettingsService tests ----

namespace {

using Lively::Grpc::Common::Proto::Settings::AppRulesSettings;
using Lively::Grpc::Common::Proto::Settings::SettingsDataModel;
using Lively::Grpc::Common::Proto::Settings::SettingsService;
using SEmpty = google::protobuf::Empty;

class FakeSettingsService final : public SettingsService::CallbackService {
public:
    grpc::ServerUnaryReactor* GetSettings(
        grpc::CallbackServerContext*, const SEmpty*, SettingsDataModel* response) override {
        *response = current;
        ++get_calls;
        return noop();
    }
    grpc::ServerUnaryReactor* SetSettings(
        grpc::CallbackServerContext*, const SettingsDataModel* request, SEmpty*) override {
        current = *request;
        seen.insert("SetSettings");
        return noop();
    }
    grpc::ServerUnaryReactor* GetAppRulesSettings(
        grpc::CallbackServerContext*, const SEmpty*, AppRulesSettings* response) override {
        auto* r = response->add_app_rules();
        r->set_app_name("chrome.exe");
        r->set_rule(Lively::Grpc::Common::Proto::Settings::AppRules::pause);
        return noop();
    }
    grpc::ServerUnaryReactor* SetAppRulesSettings(
        grpc::CallbackServerContext*, const AppRulesSettings* request, SEmpty*) override {
        seen.insert("SetAppRulesSettings:" + request->app_rules(0).app_name());
        return noop();
    }

    SettingsDataModel current;
    int get_calls = 0;
    std::set<std::string> seen;

private:
    static grpc::ServerUnaryReactor* noop() { return new UnaryNoopReactor(); }
};

} // namespace

TEST_CASE("UserSettingsClient round-trips every mapped settings field", "[rpc][settings]") {
    FakeSettingsService svc;
    // Seed the server with a distinctive settings payload.
    {
        models::SettingsModel seed;
        seed.video_player = models::LivelyMediaPlayer::vlc;
        seed.audio_volume_global = 42;
        seed.language = "zh-CN";
        seed.wallpaper_arrangement = models::WallpaperArrangement::span;
        seed.selected_display = std::make_shared<models::DisplayMonitor>();
        seed.selected_display->device_id = "\\\\?\\DISPLAY#3";
        seed.selected_display->bounds = {1920, 0, 2560, 1440};
        seed.application_theme = models::AppTheme::Light;
        seed.is_beta_opt_in = true;
        svc.current.set_saved_url(seed.saved_url);
        svc.current.set_language(seed.language);
        svc.current.set_audio_volume_global(seed.audio_volume_global);
        svc.current.set_video_player(
            Lively::Grpc::Common::Proto::Settings::MediaPlayer::mpv);
        svc.current.set_wallpaper_arrangement(
            Lively::Grpc::Common::Proto::Settings::WallpaperArrangementRule::span);
        auto* disp = svc.current.mutable_selected_display();
        disp->set_device_id("\\\\?\\DISPLAY#3");
        disp->mutable_bounds()->set_x(1920);
        disp->mutable_bounds()->set_width(2560);
        disp->mutable_bounds()->set_height(1440);
        svc.current.set_application_theme(
            Lively::Grpc::Common::Proto::Settings::AppTheme::light);
        svc.current.set_test_build(true);
    }

    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    REQUIRE(server != nullptr);

    {
        rpc::UserSettingsClient client("127.0.0.1:" + std::to_string(port));
        // Ctor loaded settings + app rules.
        REQUIRE(client.settings().has_value());
        CHECK(client.settings()->video_player == models::LivelyMediaPlayer::mpv);
        CHECK(client.settings()->audio_volume_global == 42);
        CHECK(client.settings()->language == "zh-CN");
        CHECK(client.settings()->wallpaper_arrangement == models::WallpaperArrangement::span);
        REQUIRE(client.settings()->selected_display != nullptr);
        CHECK(client.settings()->selected_display->device_id == "\\\\?\\DISPLAY#3");
        CHECK(client.settings()->selected_display->bounds.width == 2560);
        CHECK(client.settings()->application_theme == models::AppTheme::Light);
        CHECK(client.settings()->is_beta_opt_in);
        REQUIRE(client.app_rules().size() == 1);
        CHECK(client.app_rules()[0].app_name == "chrome.exe");
        CHECK(client.app_rules()[0].rule == models::AppRules::pause);

        // Save round-trip: mutate → save → server echo → reload.
        client.settings()->audio_volume_global = 88;
        client.settings()->gif_player = models::LivelyGifPlayer::libvlcExt;
        client.save_settings();
        CHECK(svc.seen.count("SetSettings") == 1);
        client.load_settings();
        CHECK(client.settings()->audio_volume_global == 88);
        CHECK(client.settings()->gif_player == models::LivelyGifPlayer::libvlcExt);
        // Untouched default survives the round-trip.
        CHECK(client.settings()->saved_url == "https://www.youtube.com/watch?v=aqz-KE-bpKQ");

        // App-rules save.
        client.app_rules().push_back({"vlc.exe", models::AppRules::ignore});
        client.save_app_rules();
        CHECK(svc.seen.count("SetAppRulesSettings:chrome.exe") == 1);

        // Async variants.
        client.load_app_rules_async().get();
        client.settings()->tile_size = 7;
        client.save_settings_async().get();
        client.load_settings_async().get();
        CHECK(client.settings()->tile_size == 7);
        REQUIRE(client.app_rules().size() >= 1);
    } // dtor
}

// Cross-language oracle: run the identical request program against the ported
// C++ client+server AND against a real C# server (Lively.Grpc.Common bindings,
// driven by `tools/csharp_probe grpcserver`). Both servers write REC lines in
// the same canonical format; the sets must be identical — proving the proto
// contract port is wire-compatible with the original C# ecosystem.
TEST_CASE("C++ client is wire-compatible with the real C# server", "[.crosslang]") {
    if (!file_exists(LIVELY_PROBE_EXE)) {
        FAIL("csharp_probe executable not found at " LIVELY_PROBE_EXE
             "; build it first: dotnet build tools/csharp_probe -c Release");
    }

    const std::string work = std::string(LIVELY_CROSSLANG_DIR);
    std::filesystem::create_directories(work);
    const std::string port_file = work + "/grpc_port.txt";
    const std::string stop_file = port_file + ".stop";
    const std::string record_file = work + "/grpc_records.txt";
    for (const auto* f : { port_file.c_str(), stop_file.c_str(), record_file.c_str() }) {
        std::remove(f);
    }

    // 1. C++ in-process server: run the request program, keep its records.
    std::set<std::string> cpp_records;
    {
        auto cpp_server = TestServer::start();
        rpc::CommandsClient client("127.0.0.1:" + cpp_server.port);
        run_request_program(client);
        cpp_records = cpp_server.service->seen;
    }

    // 2. C# oracle server: launch detached, wait for the port file.
    std::string start_cmd = "start \"\" /min \"" LIVELY_PROBE_EXE
                            "\" grpcserver \"" + port_file + "\" \"" + record_file + "\"";
    REQUIRE(std::system(start_cmd.c_str()) == 0);

    std::string port_str;
    for (int i = 0; i < 150 && port_str.empty(); ++i) {   // up to 15 s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::ifstream pf(port_file);
        if (pf.good()) std::getline(pf, port_str);
    }
    REQUIRE(!port_str.empty());

    // 3. Same program, C# server.
    {
        rpc::CommandsClient client("127.0.0.1:" + port_str);
        run_request_program(client);
    }

    // 4. Stop the server; wait until it removed the port file.
    { std::ofstream stop(stop_file); }
    for (int i = 0; i < 100 && file_exists(port_file); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 5. The C# server must have observed exactly what the C++ server did.
    auto cs_records = load_golden_lines(record_file);
    REQUIRE(cs_records.size() == cpp_records.size());
    for (const auto& rec : cpp_records) {
        INFO("missing from C# oracle: " << rec);
        CHECK(cs_records.count(rec) == 1);
    }
}

#endif // LIVELY_RPC_ENABLED
