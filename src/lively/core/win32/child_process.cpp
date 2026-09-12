#include <lively/core/win32/child_process.h>

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>

#include <processthreadsapi.h>
#endif

namespace lively::core::win32 {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty())
        return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        out.data(), size);
    return out;
}

} // namespace

ChildProcess::~ChildProcess() {
    close_handles();
}

void ChildProcess::close_handles() {
    if (stdout_read_)
        CloseHandle(stdout_read_);
    if (stdout_write_)
        CloseHandle(stdout_write_);
    if (thread_)
        CloseHandle(thread_);
    if (process_)
        CloseHandle(process_);
    stdout_read_ = stdout_write_ = thread_ = process_ = nullptr;
}

void* ChildProcess::process_handle() const {
    return process_;
}

bool ChildProcess::start(const std::string& exe, const std::string& arguments,
                         const std::string& working_dir) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    if (!CreatePipe(&stdout_read_, &stdout_write_, &attributes, 0))
        return false;
    // The read end must not be inherited, or the pipe never reports EOF.
    SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.hStdOutput = stdout_write_;
    // RedirectStandardError and RedirectStandardInput are both false in the C#, so
    // the child must INHERIT the console's stderr and stdin. Passing NULL here would
    // be wrong: with STARTF_USESTDHANDLES the null is taken literally, leaving mpv
    // with an invalid stderr handle instead of the console it would normally write
    // its log to — and mpv's log is exactly what names a rejected option.
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    // The player creates its own window; the C# does not set a window style, so
    // the child's first ShowWindow call decides. SW_SHOWNORMAL keeps that true.
    startup.wShowWindow = SW_SHOWNORMAL;

    PROCESS_INFORMATION info{};
    // lpApplicationName is set *and* the command line carries the quoted path:
    // .NET does the same (it resolves the file itself and passes the argv[0]
    // token through), and omitting the token would shift the child's argv.
    std::wstring command_line = L"\"" + widen(exe) + L"\"";
    if (!arguments.empty())
        command_line += L" " + widen(arguments);
    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    const std::wstring wide_working_dir = widen(working_dir);
    const BOOL ok = CreateProcessW(
        widen(exe).c_str(),
        mutable_command_line.data(),
        nullptr, nullptr,
        TRUE,                     // inherit the stdout pipe
        0,
        nullptr,
        wide_working_dir.empty() ? nullptr : wide_working_dir.c_str(),
        &startup,
        &info);

    // The parent's copy of the write end must be closed either way, otherwise the
    // read end never sees EOF when the child exits.
    CloseHandle(stdout_write_);
    stdout_write_ = nullptr;

    if (!ok) {
        CloseHandle(stdout_read_);
        stdout_read_ = nullptr;
        return false;
    }

    process_ = info.hProcess;
    thread_ = info.hThread;
    pid_ = static_cast<int>(info.dwProcessId);
    return true;
}

bool ChildProcess::has_exited() {
    if (process_ == nullptr)
        return true;
    DWORD code = 0;
    if (!GetExitCodeProcess(process_, &code))
        return true;
    if (code == STILL_ACTIVE)
        return false;
    cached_exit_code_ = static_cast<int>(code);
    exit_code_cached_ = true;
    return true;
}

int ChildProcess::exit_code() {
    if (exit_code_cached_)
        return cached_exit_code_;
    if (process_ == nullptr)
        return -1;
    DWORD code = 0;
    if (GetExitCodeProcess(process_, &code) && code != STILL_ACTIVE) {
        cached_exit_code_ = static_cast<int>(code);
        exit_code_cached_ = true;
    }
    return cached_exit_code_;
}

void ChildProcess::kill() {
    if (process_ != nullptr) {
        // C# try/catch swallows a kill on an already-dead process.
        TerminateProcess(process_, 1);
    }
}

std::string ChildProcess::read_available_output() {
    if (stdout_read_ == nullptr)
        return std::string();

    std::string chunk;
    DWORD available = 0;
    while (PeekNamedPipe(stdout_read_, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!ReadFile(stdout_read_, buffer.data(), available, &read, nullptr) || read == 0)
            break;
        buffer.resize(read);
        chunk += buffer;
    }
    output_ += chunk;
    return chunk;
}

Hwnd find_window_by_process_id(int pid) {
    struct Search {
        int pid;
        Hwnd found;
    } search{pid, nullptr};

    EnumWindows([](HWND top, LPARAM lparam) -> BOOL {
        auto* state = reinterpret_cast<Search*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(top, &window_pid);
        if (static_cast<int>(window_pid) == state->pid && IsWindowVisible(top)) {
            state->found = top;
            return FALSE;   // C# stops at the first match
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));

    return search.found;
}

bool wait_for_input_idle(void* process_handle, std::uint32_t timeout_ms) {
    if (process_handle == nullptr)
        return false;
    const DWORD result = WaitForInputIdle(static_cast<HANDLE>(process_handle), timeout_ms);
    // 0 = idle (or already exited), WAIT_TIMEOUT = still busy. The C# loops while
    // the result is not true, so a timeout means "try again".
    return result == 0;
}

Hwnd wait_for_process_window(int pid, int timeout_ticks, bool* cancelled,
                             const volatile bool* cancel_flag) {
    if (pid == 0)
        return nullptr;
    if (cancelled)
        *cancelled = false;

    for (int i = 0; i < timeout_ticks; ++i) {
        if (cancel_flag != nullptr && *cancel_flag) {
            if (cancelled)
                *cancelled = true;
            return nullptr;
        }
        // C#: `for (int i = 0; i < timeOut && proc.HasExited == false; i++)`
        const Hwnd found = find_window_by_process_id(pid);
        if (found != nullptr)
            return found;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nullptr;
}

#else

ChildProcess::~ChildProcess() = default;
void ChildProcess::close_handles() {}
void* ChildProcess::process_handle() const { return nullptr; }
bool ChildProcess::start(const std::string&, const std::string&, const std::string&) { return false; }
bool ChildProcess::has_exited() { return true; }
int ChildProcess::exit_code() { return -1; }
void ChildProcess::kill() {}
std::string ChildProcess::read_available_output() { return std::string(); }

Hwnd find_window_by_process_id(int) { return nullptr; }
bool wait_for_input_idle(void*, std::uint32_t) { return false; }
Hwnd wait_for_process_window(int, int, bool*, const volatile bool*) { return nullptr; }

#endif

} // namespace lively::core::win32
