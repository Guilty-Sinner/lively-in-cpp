#pragma once
// Port of Lively.Grpc.Client/ICommandsClient + CommandsClient.
//
// C# surface (ICommandsClient):
//   Task ShowUI();  CloseUI();  RestartUI();  RestartUI(string startArgs);
//   ShowDebugger(); ShutDown(); ShowScreensaver(bool); StopScreensaver();
//   ScreensaverConfigure(); ScreensaverPreview(int);
//   Task AutomationCommandAsync(string[] args);  void AutomationCommand(string[]);
//   void SaveRectUI();  Task SaveRectUIAsync();
//
// Transport note: the C# client uses GrpcDotNetNamedPipes (a managed-only
// named-pipe transport — stock gRPC C++ has no Win32 named-pipe transport).
// The *contract* — proto service, request/response schemas, method semantics —
// is ported exactly; the C++ client targets the same service over standard
// gRPC (loopback TCP or any grpc::Channel target the caller supplies).
// See tests/test_rpc.cpp: the in-process C++ server and a real C# server are
// interchangeable targets, proving wire-level equivalence of the contract.

#include <lively/task.h>

#include <memory>
#include <string>
#include <vector>

namespace lively::rpc {

class CommandsClient {
public:
    // target: any grpc target string, e.g. "127.0.0.1:54321".
    explicit CommandsClient(const std::string& target);
    ~CommandsClient();

    CommandsClient(const CommandsClient&) = delete;
    CommandsClient& operator=(const CommandsClient&) = delete;

    Task<> ShowUI();
    Task<> CloseUI();
    Task<> RestartUI();
    Task<> RestartUI(std::string start_args);
    Task<> ShowDebugger();
    Task<> ShowScreensaver(bool is_fade_in);
    Task<> StopScreensaver();
    Task<> ScreensaverConfigure();
    Task<> ScreensaverPreview(int preview_handle);
    Task<> ShutDown();
    Task<> AutomationCommandAsync(std::vector<std::string> args);

    // C# fire-and-forget overload: same blocking sync call under the hood
    // (`_ = client.AutomationCommand(request)` uses the sync stub variant).
    void AutomationCommand(const std::vector<std::string>& args);
    // C# `SaveRectUI` is sync in the interface but the call is still the
    // blocking stub variant; kept as a separate fire-and-forget-shaped API.
    void SaveRectUI();
    Task<> SaveRectUIAsync();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lively::rpc
