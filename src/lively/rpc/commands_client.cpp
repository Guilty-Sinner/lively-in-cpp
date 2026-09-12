#include <lively/rpc/commands_client.h>

#include <lively/common/constants.h>

#include <grpcpp/grpcpp.h>

#include <commands.grpc.pb.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace lively::rpc {

namespace {

using Lively::Grpc::Common::Proto::Commands::AutomationCommandRequest;
using Lively::Grpc::Common::Proto::Commands::CommandsService;
using Lively::Grpc::Common::Proto::Commands::RestartRequest;
using Lively::Grpc::Common::Proto::Commands::ScreensaverRequest;
using Lively::Grpc::Common::Proto::Commands::ScreensaverState;
using Empty = google::protobuf::Empty;

// Maps a completed gRPC callback onto the coroutine world: ok → task result,
// non-ok → RpcException (the C# stub throws grpc::RpcException in the same
// spot; Lively's callers treat any exception as "core not reachable").
void complete(const task_completion_source<>& tcs, const grpc::Status& status) {
    if (status.ok()) {
        tcs.set_result();
        return;
    }
    tcs.set_exception(std::make_exception_ptr(std::runtime_error(
        "RpcException: " + status.error_message() +
        " (code " + std::to_string(static_cast<int>(status.error_code())) + ")")));
}

// Issues one unary call via the generated callback API and returns a Task
// resolving on completion. Request/context/response are shared_ptr-owned so
// they outlive the call regardless of the issuing coroutine's lifetime —
// mirroring the C# stub, where the runtime owns the message buffers.

Task<> call_show_ui(CommandsService::Stub& stub) {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    stub.async()->ShowUI(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

} // namespace

struct CommandsClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<CommandsService::Stub> stub;
};

CommandsClient::CommandsClient(const std::string& target)
    : impl_(std::make_unique<Impl>()) {
    impl_->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    impl_->stub = CommandsService::NewStub(impl_->channel);
}

CommandsClient::~CommandsClient() = default;

// Each async method: fire the callback-API unary call, wrap the completion
// callback into a task_completion_source, co_await it. This is the direct
// analogue of the C# `await client.XAsync(request)` stubs.

Task<> CommandsClient::ShowUI() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->ShowUI(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::CloseUI() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->CloseUI(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::RestartUI() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->RestartUI(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::RestartUI(std::string start_args) {
    task_completion_source<> tcs;
    auto req = std::make_shared<RestartRequest>();
    req->set_start_args(std::move(start_args));
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->RestartUIWithArgs(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::ShowDebugger() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->ShowDebugger(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::ShowScreensaver(bool is_fade_in) {
    task_completion_source<> tcs;
    auto req = std::make_shared<ScreensaverRequest>();
    req->set_state(ScreensaverState::start);
    req->set_fade_in(is_fade_in);
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->Screensaver(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::StopScreensaver() {
    task_completion_source<> tcs;
    auto req = std::make_shared<ScreensaverRequest>();
    req->set_state(ScreensaverState::stop);
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->Screensaver(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::ScreensaverConfigure() {
    task_completion_source<> tcs;
    auto req = std::make_shared<ScreensaverRequest>();
    req->set_state(ScreensaverState::configure);
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->Screensaver(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::ScreensaverPreview(int preview_handle) {
    task_completion_source<> tcs;
    auto req = std::make_shared<ScreensaverRequest>();
    req->set_state(ScreensaverState::preview);
    req->set_preview_hwnd(preview_handle);
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->Screensaver(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::ShutDown() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->ShutDown(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

Task<> CommandsClient::AutomationCommandAsync(std::vector<std::string> args) {
    task_completion_source<> tcs;
    auto req = std::make_shared<AutomationCommandRequest>();
    for (auto& a : args) req->add_args(std::move(a));
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->AutomationCommand(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

// C# `void AutomationCommand(string[])` uses the *blocking* stub variant and
// discards the result (`_ = client.AutomationCommand(request)`), i.e. the call
// blocks until the core replies but no completion is observed by the caller.
void CommandsClient::AutomationCommand(const std::vector<std::string>& args) {
    grpc::ClientContext ctx;
    AutomationCommandRequest req;
    for (const auto& a : args) req.add_args(a);
    Empty resp;
    (void)impl_->stub->AutomationCommand(&ctx, req, &resp);
}

void CommandsClient::SaveRectUI() {
    grpc::ClientContext ctx;
    Empty resp;
    (void)impl_->stub->SaveRectUI(&ctx, Empty{}, &resp);
}

Task<> CommandsClient::SaveRectUIAsync() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->SaveRectUI(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) { complete(tcs, s); });
    co_await tcs.task();
}

} // namespace lively::rpc
