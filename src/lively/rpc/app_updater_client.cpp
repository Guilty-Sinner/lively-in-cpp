#include <lively/rpc/app_updater_client.h>

#include <grpcpp/grpcpp.h>

#include <update.grpc.pb.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace lively::rpc {

namespace {

using Lively::Grpc::Common::Proto::Update::GetLatestReleaseRequest;
using Lively::Grpc::Common::Proto::Update::GetLatestReleaseResponse;
using Lively::Grpc::Common::Proto::Update::ReleaseChannel;
using Lively::Grpc::Common::Proto::Update::SwitchReleaseChannelRequest;
using Lively::Grpc::Common::Proto::Update::UpdateResponse;
using Lively::Grpc::Common::Proto::Update::UpdateService;
using Empty = google::protobuf::Empty;

std::runtime_error rpc_error(const grpc::Status& s) {
    return std::runtime_error("RpcException: " + s.error_message() +
                              " (code " + std::to_string(static_cast<int>(s.error_code())) + ")");
}

} // namespace

std::optional<AppVersion> AppVersion::parse(const std::string& s) {
    // C# new Version(str): 1–4 dot-separated ints; null/empty → C# null (nullopt).
    if (s.empty()) return std::nullopt;
    AppVersion v;
    std::vector<int> parts;
    std::size_t start = 0;
    while (true) {
        const auto dot = s.find('.', start);
        parts.push_back(std::stoi(s.substr(start, dot - start)));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (parts.size() > 4) throw std::runtime_error("Version string too long: " + s);
    v.major = parts[0];
    if (parts.size() > 1) v.minor = parts[1];
    if (parts.size() > 2) v.build = parts[2];
    if (parts.size() > 3) v.revision = parts[3];
    return v;
}

struct AppUpdaterClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<UpdateService::Stub> stub;
    std::jthread update_checked_thread;
};

AppUpdaterClient::AppUpdaterClient(const std::string& target)
    : impl_(std::make_unique<Impl>()) {
    impl_->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    impl_->stub = UpdateService::NewStub(impl_->channel);

    // C# ctor: blocking initial status refresh, then the stream loop.
    refresh_status();
    impl_->update_checked_thread =
        std::jthread([this] { subscribe_update_checked_loop(); });
}

AppUpdaterClient::~AppUpdaterClient() {
    if (impl_ && impl_->update_checked_thread.joinable()) {
        impl_->update_checked_thread.request_stop();
    }
}

// C# UpdateStatusRefresh — refresh_ fields from GetUpdateStatus.
void AppUpdaterClient::refresh_status() {
    grpc::ClientContext ctx;
    Empty req;
    UpdateResponse resp;
    if (grpc::Status s = impl_->stub->GetUpdateStatus(&ctx, req, &resp); !s.ok()) {
        throw rpc_error(s);
    }
    status_ = static_cast<AppUpdateStatus>(static_cast<int>(resp.status()));
    // google.protobuf.Timestamp → epoch seconds (C# .ToLocalTime(); epoch form
    // is timezone-free for comparisons — UI formatting applies tz later).
    if (resp.has_time()) {
        last_check_time_epoch_ = resp.time().seconds();
    } else {
        last_check_time_epoch_ = 0; // C# DateTime.MinValue sentinel
    }
    last_check_changelog_ = resp.changelog();
    // C# wraps the parse in try/catch — invalid strings leave the field null.
    try { last_check_version_ = AppVersion::parse(resp.version()); }
    catch (...) { last_check_version_ = std::nullopt; }
    last_check_uri_ = resp.url();       // empty == C# null
    last_check_file_name_ = resp.file_name();
}

Task<> AppUpdaterClient::check_update() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CheckUpdate(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
        });
    co_await tcs.task();
}

Task<> AppUpdaterClient::start_update() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->StartUpdate(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
        });
    co_await tcs.task();
}

Task<LatestRelease> AppUpdaterClient::get_latest_release(bool is_beta) {
    task_completion_source<LatestRelease> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<GetLatestReleaseRequest>();
    req->set_channel(is_beta ? ReleaseChannel::beta : ReleaseChannel::stable);
    auto resp = std::make_shared<GetLatestReleaseResponse>();
    impl_->stub->async()->GetLatestRelease(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (!s.ok()) {
                tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
                return;
            }
            LatestRelease out;
            out.url = resp->url();          // empty == C# null
            out.file_name = resp->file_name();
            // C# ternaries: null when the string is null/empty.
            if (!resp->version().empty()) {
                try { out.app_version = AppVersion::parse(resp->version()); }
                catch (...) { out.app_version = std::nullopt; }
            }
            tcs.set_result(std::move(out));
        });
    co_return co_await tcs.task();
}

Task<> AppUpdaterClient::switch_release_channel(bool is_beta) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<SwitchReleaseChannelRequest>();
    req->set_channel(is_beta ? ReleaseChannel::beta : ReleaseChannel::stable);
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->SwitchReleaseChannel(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
        });
    co_await tcs.task();
}

// C# SubscribeUpdateCheckedStream: on each ping → UpdateStatusRefresh() +
// raise UpdateChecked(status, version, time, uri, fileName).
void AppUpdaterClient::subscribe_update_checked_loop() {
    try {
        grpc::ClientContext ctx;
        Empty req;
        auto reader = impl_->stub->SubscribeUpdateChecked(&ctx, req);
        Empty item;
        while (!impl_->update_checked_thread.get_stop_token().stop_requested()) {
            if (!reader->Read(&item)) break;
            refresh_status();
            update_checked.raise(AppUpdaterEventArgs{
                status_, last_check_version_, last_check_time_epoch_,
                last_check_uri_, last_check_file_name_});
        }
        (void)reader->Finish();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "update_checked stream: %s\n", e.what());
    }
}

} // namespace lively::rpc
