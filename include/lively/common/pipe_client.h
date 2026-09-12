#pragma once
// Port of Lively.Common/Helpers/IPC/PipeClient.cs — one-shot named-pipe
// message send.
//
// C# semantics (NamedPipeClientStream(".", channelName, Out)):
//   * Connect(0): fail IMMEDIATELY if no server is listening (no retry wait) —
//     C# throws TimeoutException; here we return false and report GetLastError.
//   * Write the full message, flush, close.
//
// Lively uses this to wake the running instance (e.g. second-launch
// "show UI" command), mirroring the v1.x pipe protocol.
//
// Reference C#: Lively.Common/Helpers/IPC/PipeClient.cs

#include <string>

namespace lively::common {

class PipeClient {
public:
    // C# void SendMessage(string channelName, string msg). (Named send_message:
    // windows.h defines SendMessage as a macro — the C# name is unusable.)
    // Returns true on success; false when no server is listening (C#
    // TimeoutException path — callers treat both as "core not running").
    static bool send_message(const std::string& channel_name, const std::string& msg);

    // Wide-char overload for the Constants.SingleInstance pipe names
    // (which are std::wstring in the constants port).
    static bool send_message(const std::wstring& channel_name, const std::string& msg);
};

} // namespace lively::common
