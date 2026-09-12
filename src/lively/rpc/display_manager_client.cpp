#include <lively/rpc/display_manager_client.h>

#include <grpcpp/grpcpp.h>

#include <display.grpc.pb.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lively::rpc {

namespace {

using Lively::Grpc::Common::Proto::Display::DisplayService;
using Lively::Grpc::Common::Proto::Display::GetScreensResponse;
using Lively::Grpc::Common::Proto::Display::Rectangle;
using Lively::Grpc::Common::Proto::Display::ScreenData;
using Empty = google::protobuf::Empty;

void throw_if_failed(const grpc::Status& status) {
    if (!status.ok()) {
        throw std::runtime_error("RpcException: " + status.error_message() +
                                 " (code " + std::to_string(static_cast<int>(status.error_code())) + ")");
    }
}

models::DisplayMonitor to_display(const ScreenData& s) {
    models::DisplayMonitor out;
    out.device_id = s.device_id();
    out.device_name = s.device_name();
    out.display_name = s.display_name();
    out.h_monitor = s.h_monitor();
    out.is_primary = s.is_primary();
    out.index = s.index();
    out.bounds = {s.bounds().x(), s.bounds().y(), s.bounds().width(), s.bounds().height()};
    out.working_area = {s.working_area().x(), s.working_area().y(),
                        s.working_area().width(), s.working_area().height()};
    return out;
}

models::Rectangle to_rect(const Rectangle& r) {
    return {r.x(), r.y(), r.width(), r.height()};
}

} // namespace

struct DisplayManagerClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<DisplayService::Stub> stub;

    mutable std::mutex monitors_mutex;
    std::vector<models::DisplayMonitor> monitors;

    std::jthread display_changed_thread;
};

DisplayManagerClient::DisplayManagerClient(const std::string& target)
    : target_(target), impl_(std::make_unique<Impl>()) {
    impl_->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    impl_->stub = DisplayService::NewStub(impl_->channel);

    // C# ctor Task.Run(...).Wait(): initial display snapshots.
    {
        grpc::ClientContext ctx;
        Empty req;
        GetScreensResponse resp;
        throw_if_failed(impl_->stub->GetScreens(&ctx, req, &resp));
        std::vector<models::DisplayMonitor> initial;
        for (const auto& screen : resp.screens()) {
            initial.push_back(to_display(screen));
        }
        std::lock_guard<std::mutex> lk(impl_->monitors_mutex);
        impl_->monitors = std::move(initial);
    }
    {
        grpc::ClientContext ctx;
        Empty req;
        Rectangle resp;
        throw_if_failed(impl_->stub->GetVirtualScreenBounds(&ctx, req, &resp));
        virtual_screen_bounds_ = to_rect(resp);
    }

    impl_->display_changed_thread =
        std::jthread([this] { subscribe_display_changed_loop(); });
}

DisplayManagerClient::~DisplayManagerClient() {
    if (impl_ && impl_->display_changed_thread.joinable()) {
        impl_->display_changed_thread.request_stop();
    }
    // join happens in the jthread member destructor.
}

std::vector<models::DisplayMonitor> DisplayManagerClient::display_monitors() const {
    std::lock_guard<std::mutex> lk(impl_->monitors_mutex);
    return impl_->monitors;
}

std::optional<models::DisplayMonitor> DisplayManagerClient::primary_monitor() const {
    // C#: displayMonitors.FirstOrDefault(x => x.IsPrimary) — nullopt when absent.
    std::lock_guard<std::mutex> lk(impl_->monitors_mutex);
    const auto it = std::find_if(impl_->monitors.begin(), impl_->monitors.end(),
                                 [](const models::DisplayMonitor& m) { return m.is_primary; });
    if (it == impl_->monitors.end()) return std::nullopt;
    return *it;
}

void DisplayManagerClient::subscribe_display_changed_loop() {
    try {
        grpc::ClientContext ctx;
        Empty req;
        auto reader = impl_->stub->SubscribeDisplayChanged(&ctx, req);
        Empty item;
        while (!impl_->display_changed_thread.get_stop_token().stop_requested()) {
            if (!reader->Read(&item)) break;
            // C# handler: clear + re-fetch GetScreens + GetVirtualScreenBounds,
            // recompute PrimaryMonitor, raise event (under displayChangedLock —
            // here the guarded swap is atomic and the event is raised outside).
            {
                grpc::ClientContext sctx;
                Empty sreq;
                GetScreensResponse sresp;
                throw_if_failed(impl_->stub->GetScreens(&sctx, sreq, &sresp));
                std::vector<models::DisplayMonitor> fresh;
                for (const auto& screen : sresp.screens()) {
                    fresh.push_back(to_display(screen));
                }
                grpc::ClientContext bctx;
                Empty breq;
                Rectangle bresp;
                throw_if_failed(impl_->stub->GetVirtualScreenBounds(&bctx, breq, &bresp));
                virtual_screen_bounds_ = to_rect(bresp);

                {
                    std::lock_guard<std::mutex> lk(impl_->monitors_mutex);
                    impl_->monitors = std::move(fresh);
                }
            }
            display_changed.raise(DisplayManagerClient::Unit{});
        }
        (void)reader->Finish();
    } catch (const std::exception& e) {
        // C# catch: Console.WriteLine(e.ToString()).
        std::fprintf(stderr, "display_changed stream: %s\n", e.what());
    }
}

} // namespace lively::rpc
