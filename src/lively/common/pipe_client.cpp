#include <lively/common/pipe_client.h>

#include <windows.h>

#include <string>

namespace lively::common {

namespace {

bool send_impl(const std::wstring& channel_name, const std::string& msg) {
    // C# NamedPipeClientStream(".", name, PipeDirection.Out) →
    // GENERIC_WRITE access on \\.\pipe\<name>.
    const std::wstring full_path = L"\\\\.\\pipe\\" + channel_name;
    HANDLE pipe = CreateFileW(full_path.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false; // C# Connect(0) timeout path: no server listening.
    }

    // C# writes the raw string (no framing, no length prefix); the server
    // reads to end-of-message on client disconnect.
    DWORD written = 0;
    const BOOL ok = WriteFile(pipe, msg.data(), static_cast<DWORD>(msg.size()),
                              &written, nullptr);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok != FALSE && written == msg.size();
}

} // namespace

bool PipeClient::send_message(const std::string& channel_name, const std::string& msg) {
    return send_impl(std::wstring(channel_name.begin(), channel_name.end()), msg);
}

bool PipeClient::send_message(const std::wstring& channel_name, const std::string& msg) {
    return send_impl(channel_name, msg);
}

} // namespace lively::common
