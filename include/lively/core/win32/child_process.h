#pragma once
// Port of what Lively uses System.Diagnostics.Process for: launch a player
// executable with a raw argument string, watch for the window it creates, read
// its redirected stdout, and kill it.
//
// The C# never uses a shell: `UseShellExecute = false`, a separate
// `FileName`, and `Arguments` handed through verbatim (the mpv host builds one
// long option string, quotes included). So this is CreateProcessW with
// lpApplicationName set and lpCommandLine = "<exe>" + the argument string —
// no shell quoting pass, because a quoting pass is exactly what would break the
// `--config-dir="..."` and path-with-quote cases the mpv transcript pins.
//
// stdout is redirected (the C# sets RedirectStandardOutput and parses
// `Screenshot: '<path>'` out of it for ScreenCapture), so the pipe is read
// continuously into a buffer. stderr and stdin are left attached.

#include <lively/core/win32/window_util.h>

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lively::core::win32 {

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Launches `exe` with `arguments` as the raw command line. `working_dir` is
    // the C# StartInfo.WorkingDirectory (mpv runs from plugins/mpv so its
    // relative fonts/ scripts resolve).
    bool start(const std::string& exe, const std::string& arguments,
               const std::string& working_dir);

    bool started() const { return process_ != nullptr; }
    int pid() const { return pid_; }
    void* process_handle() const;

    // HasExited + ExitCode, cached like the C# `exitCode` field.
    bool has_exited();
    int exit_code();

    // Process.Kill(): TerminateProcess. The C# wraps it in try/catch and ignores
    // failures (the process may already be gone).
    void kill();

    // Non-blocking drain of the redirected stdout. The C# receives the same bytes
    // through OutputDataReceived on a background thread; here the caller polls,
    // which removes the ordering problem between the read callback and the
    // cancellation the C# has to work around in ScreenCapture.
    std::string read_available_output();
    // Everything read so far, for the screenshot-line search.
    const std::string& output_buffer() const { return output_; }

#ifdef _WIN32
    HANDLE process() const { return process_; }
#endif

private:
    void close_handles();

#ifdef _WIN32
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE stdout_read_ = nullptr;
    HANDLE stdout_write_ = nullptr;
#endif
    int pid_ = 0;
    int cached_exit_code_ = -1;
    bool exit_code_cached_ = false;
    std::string output_;
};

// ProcessExtensions.WaitForProcesWindow: wait for the child's message loop, then
// poll for its first visible top-level window.
//
// The C# polls once per millisecond up to `timeOut` ticks, and `nativeSearch`
// (always true for the player hosts) selects FindWindowByProcessId rather than
// Process.MainWindowHandle — the latter returns zero for many .NET Core children
// and is the reason the native search exists at all.
//
// Returns null on timeout. `cancelled` reports the C# cts.CheckCancellation —
// Close() cancels this wait before sending `quit`, so a wallpaper closed while
// still starting does not resurrect itself when the window finally appears.
Hwnd wait_for_process_window(int pid, int timeout_ticks, bool* cancelled = nullptr,
                             const volatile bool* cancel_flag = nullptr);

// ProcessExtensions.FindWindowByProcessId: first *visible* top-level window of
// the process. EnumWindows order is z-order, so this is the frontmost one.
Hwnd find_window_by_process_id(int pid);

// Process.WaitForInputIdle — user32's WaitForInputIdle, waiting for the child's
// message queue to go idle. Returns when the queue is idle, the timeout expires,
// or the process exits (which the C# treats as "keep going" because the loop
// condition checks HasExited separately).
bool wait_for_input_idle(void* process_handle, std::uint32_t timeout_ms);

} // namespace lively::core::win32
