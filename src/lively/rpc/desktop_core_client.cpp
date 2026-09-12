#include <lively/rpc/desktop_core_client.h>

#include <lively/common/exceptions.h>

#include <grpcpp/grpcpp.h>

#include <wallpaper.grpc.pb.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lively::rpc {

namespace {

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
using Lively::Grpc::Common::Proto::Desktop::WallpaperCategory;
using Lively::Grpc::Common::Proto::Desktop::WallpaperErrorResponse;
using Lively::Grpc::Common::Proto::Desktop::WallpaperMessageRequest;
using Lively::Grpc::Common::Proto::Desktop::WallpaperScreenshotRequest;
using Empty = google::protobuf::Empty;

void throw_if_failed(const grpc::Status& status) {
    if (!status.ok()) {
        throw std::runtime_error("RpcException: " + status.error_message() +
                                 " (code " + std::to_string(static_cast<int>(status.error_code())) + ")");
    }
}

// WinDesktopCoreClient.GetException — ErrorCategory → typed exception.
std::exception_ptr get_exception(const WallpaperErrorResponse& error) {
    try {
        switch (error.error()) {
            case ErrorCategory::workerw:
                throw common::WorkerWException(error.error_msg());
            case ErrorCategory::wallpaper_not_found:
                throw common::WallpaperNotFoundException(error.error_msg());
            case ErrorCategory::wallpaper_not_allowed:
                throw common::WallpaperNotAllowedException(error.error_msg());
            case ErrorCategory::wallpaper_plugin_not_found:
                throw common::WallpaperPluginNotFoundException(error.error_msg());
            case ErrorCategory::wallpaper_plugin_fail:
                throw common::WallpaperPluginException(error.error_msg());
            case ErrorCategory::wallpaper_plugin_media_codec_missing:
                throw common::WallpaperPluginMediaCodecException(error.error_msg());
            case ErrorCategory::screen_not_found:
                throw common::ScreenNotFoundException(error.error_msg());
            case ErrorCategory::wallpaper_webview2_not_found:
                throw common::WallpaperWebView2NotFoundException(error.error_msg());
            case ErrorCategory::wallpaper_file_error:
                throw common::WallpaperFileException(error.error_msg());
            default:
                throw std::runtime_error(error.error_msg());
        }
    } catch (...) {
        return std::current_exception();
    }
}

// proto ScreenData → ported DisplayMonitor (same field mapping as C#).
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

} // namespace

struct DesktopCoreClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<DesktopService::Stub> stub;

    mutable std::mutex wallpapers_mutex;
    std::vector<WallpaperData> wallpapers;

    // Subscription loops: C# Task.Run + CancellationTokenSource → jthread.
    std::jthread wallpaper_changed_thread;
    std::jthread wallpaper_error_thread;
};

DesktopCoreClient::DesktopCoreClient(const std::string& target)
    : target_(target), impl_(std::make_unique<Impl>()) {
    impl_->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    impl_->stub = DesktopService::NewStub(impl_->channel);

    // C# ctor: Task.Run(...).Wait() — initial snapshots before returning.
    {
        grpc::ClientContext ctx;
        GetCoreStatsResponse stats;
        Empty req;
        throw_if_failed(impl_->stub->GetCoreStats(&ctx, req, &stats));
        base_directory_ = stats.base_directory();
        assembly_version_ = stats.assembly_version();
        is_core_initialized_ = stats.is_core_initialized();
    }
    {
        grpc::ClientContext ctx;
        Empty req;
        auto reader = impl_->stub->GetWallpapers(&ctx, req);
        GetWallpapersResponse item;
        std::vector<WallpaperData> initial;
        while (reader->Read(&item)) {
            WallpaperData w;
            w.lively_info_folder_path = item.lively_info_path();
            w.lively_property_copy_path = item.property_copy_path();
            w.preview_path = item.preview_path();
            w.thumbnail_path = item.thumbnail_path();
            w.category = static_cast<models::WallpaperType>(static_cast<int>(item.category()));
            if (item.has_screen()) w.display = to_display(item.screen());
            initial.push_back(std::move(w));
        }
        auto status = reader->Finish();
        throw_if_failed(status);
        std::lock_guard<std::mutex> lk(impl_->wallpapers_mutex);
        impl_->wallpapers = std::move(initial);
    }

    // C# ctor: Task.Run(SubscribeWallpaperChangedStream / ...ErrorStream).
    impl_->wallpaper_changed_thread = std::jthread([this] { subscribe_wallpaper_changed_loop(); });
    impl_->wallpaper_error_thread = std::jthread([this] { subscribe_wallpaper_error_loop(); });
}

DesktopCoreClient::~DesktopCoreClient() {
    // C# Dispose: cancel both subscription tokens, then Task.WaitAll.
    // jthread's stop_token propagates cancellation; join happens in the
    // jthread destructor. Reader contexts must outlive Read() — the loops
    // create them locally and check stop_requested before each Read.
    if (impl_) {
        if (impl_->wallpaper_changed_thread.joinable()) {
            impl_->wallpaper_changed_thread.request_stop();
        }
        if (impl_->wallpaper_error_thread.joinable()) {
            impl_->wallpaper_error_thread.request_stop();
        }
        // join on destruction of the jthread members (after ~Impl body).
    }
}

std::vector<WallpaperData> DesktopCoreClient::wallpapers() const {
    std::lock_guard<std::mutex> lk(impl_->wallpapers_mutex);
    return impl_->wallpapers;
}

Task<> DesktopCoreClient::set_wallpaper(const std::string& lively_info_path,
                                        const std::string& monitor_id) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<SetWallpaperRequest>();
    req->set_lively_info_path(lively_info_path);
    req->set_monitor_id(monitor_id);
    // C# SetWallpaper(string, string) also sets Type = LibraryItemCategory.Ready.
    req->set_type(Lively::Grpc::Common::Proto::Desktop::LibraryItemCategory::ready);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->SetWallpaper(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error(
                "RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<bool> DesktopCoreClient::edit_wallpaper(const std::string& lively_info_path) {
    task_completion_source<bool> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<EditWallpaperRequest>();
    req->set_lively_info_path(lively_info_path);
    auto resp = std::make_shared<EditWallpaperResponse>();
    impl_->stub->async()->EditWallpaper(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp, this](grpc::Status s) {
            if (s.ok()) {
                // C#: if (response.Error != null) WallpaperError?.Invoke(...);
                if (resp->has_error()) {
                    wallpaper_error.raise(get_exception(resp->error()));
                }
                tcs.set_result(resp->is_success());
            } else {
                tcs.set_exception(std::make_exception_ptr(std::runtime_error(
                    "RpcException: " + s.error_message())));
            }
        });
    co_return co_await tcs.task();
}

Task<std::string> DesktopCoreClient::create_wallpaper(const std::string& file_path,
                                                      models::WallpaperType type,
                                                      const std::string& arguments) {
    task_completion_source<std::string> tcs;
    auto req = std::make_shared<CreateWallpaperRequest>();
    req->set_file_path(file_path);
    req->set_category(static_cast<WallpaperCategory>(static_cast<int>(type)));
    // C#: string.IsNullOrWhiteSpace(arguments) ? string.Empty : arguments
    req->set_arguments(arguments.empty() ? std::string() : arguments);
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<CreateWallpaperResponse>();
    impl_->stub->async()->CreateWallpaper(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp, this](grpc::Status s) {
            if (s.ok()) {
                if (resp->has_error()) {
                    wallpaper_error.raise(get_exception(resp->error()));
                }
                tcs.set_result(resp->is_success() ? resp->lively_info_path() : std::string());
            } else {
                tcs.set_exception(std::make_exception_ptr(std::runtime_error(
                    "RpcException: " + s.error_message())));
            }
        });
    co_return co_await tcs.task();
}

Task<> DesktopCoreClient::close_all_wallpapers() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<CloseAllWallpapersRequest>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CloseAllWallpapers(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<> DesktopCoreClient::close_wallpaper(const std::string& monitor_id) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<CloseWallpaperMonitorRequest>();
    req->set_monitor_id(monitor_id);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CloseWallpaperMonitor(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<> DesktopCoreClient::close_wallpaper_library(const std::string& lively_info_path) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<CloseWallpaperLibraryRequest>();
    req->set_lively_info_path(lively_info_path);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CloseWallpaperLibrary(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<> DesktopCoreClient::close_wallpaper_category(models::WallpaperType type) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<CloseWallpaperCategoryRequest>();
    req->set_category(static_cast<WallpaperCategory>(static_cast<int>(type)));
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CloseWallpaperCategory(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<> DesktopCoreClient::preview_wallpaper(const std::string& lively_info_path) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<PreviewWallpaperRequest>();
    req->set_lively_info_path(lively_info_path);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->PreviewWallpaper(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

Task<> DesktopCoreClient::take_screenshot(const std::string& monitor_id,
                                          const std::string& save_path) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<WallpaperScreenshotRequest>();
    req->set_monitor_id(monitor_id);
    req->set_save_path(save_path);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->TakeScreenshot(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(std::runtime_error("RpcException: " + s.error_message())));
        });
    co_await tcs.task();
}

void DesktopCoreClient::send_message_wallpaper(const std::string& monitor_id,
                                               const std::string& lively_info_path,
                                               const models::IpcMessage& msg) {
    // C# SendMessageWallpaper uses the *blocking* stub (void method).
    grpc::ClientContext ctx;
    WallpaperMessageRequest req;
    req.set_monitor_id(monitor_id);
    req.set_lively_info_path(lively_info_path);
    req.set_msg(models::serialize(msg)); // JsonUtil.Serialize → JsonConvert.SerializeObject
    Empty resp;
    (void)impl_->stub->SendMessageWallpaper(&ctx, req, &resp);
}

// Server-streaming loops. C# pattern:
//   using var call = client.SubscribeX(new Empty());
//   while (await call.ResponseStream.MoveNext(token)) { ... }
// C++ pattern: blocking ClientReader on a jthread; cancellation checks before
// each Read (stop_token) — Close() on the context is the async interrupt.
void DesktopCoreClient::subscribe_wallpaper_changed_loop() {
    try {
        grpc::ClientContext ctx;
        Empty req;
        auto reader = impl_->stub->SubscribeWallpaperChanged(&ctx, req);
        Empty item; // stream of google.protobuf.Empty (server pushes a ping)
        while (!impl_->wallpaper_changed_thread.get_stop_token().stop_requested()) {
            if (!reader->Read(&item)) break;
            // C# handler: clear + reload from GetWallpapers, then raise event
            // under wallpaperChangedLock. The reload below is the same RPC.
            {
                grpc::ClientContext rctx;
                Empty rreq;
                auto rreader = impl_->stub->GetWallpapers(&rctx, rreq);
                GetWallpapersResponse ritem;
                std::vector<WallpaperData> fresh;
                while (rreader->Read(&ritem)) {
                    WallpaperData w;
                    w.lively_info_folder_path = ritem.lively_info_path();
                    w.lively_property_copy_path = ritem.property_copy_path();
                    w.preview_path = ritem.preview_path();
                    w.thumbnail_path = ritem.thumbnail_path();
                    w.category = static_cast<models::WallpaperType>(static_cast<int>(ritem.category()));
                    if (ritem.has_screen()) w.display = to_display(ritem.screen());
                    fresh.push_back(std::move(w));
                }
                throw_if_failed(rreader->Finish());
                {
                    std::lock_guard<std::mutex> lk(impl_->wallpapers_mutex);
                    impl_->wallpapers = std::move(fresh);
                }
            }
            wallpaper_changed.raise(DesktopCoreClient::Unit{});
        }
        (void)reader->Finish();
    } catch (const std::exception& e) {
        // C# catch: Console.WriteLine(e.ToString()) — swallow, log.
        std::fprintf(stderr, "wallpaper_changed stream: %s\n", e.what());
    }
}

void DesktopCoreClient::subscribe_wallpaper_error_loop() {
    try {
        grpc::ClientContext ctx;
        Empty req;
        auto reader = impl_->stub->SubscribeWallpaperError(&ctx, req);
        WallpaperErrorResponse item;
        while (!impl_->wallpaper_error_thread.get_stop_token().stop_requested()) {
            if (!reader->Read(&item)) break;
            wallpaper_error.raise(get_exception(item));
        }
        (void)reader->Finish();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wallpaper_error stream: %s\n", e.what());
    }
}

} // namespace lively::rpc
