#include "lively/common/app_lifecycle.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>

namespace lively::common {

namespace {

std::wstring to_lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return s;
}

} // namespace

bool AppLifeCycleUtil::IsAppMutexRunning(const std::wstring& mutex_name) {
    HANDLE mutex = ::OpenMutexW(SYNCHRONIZE, FALSE, mutex_name.c_str());
    if (mutex) {
        ::CloseHandle(mutex);
        return true;
    }
    return false;
}

bool AppLifeCycleUtil::IsAppProcessRunning(const std::wstring& process_name) {
    // C# GetProcessesByName compares image name without extension,
    // case-insensitive.
    const std::wstring want = to_lower(process_name);
    const std::wstring want_exe = want + L".exe";

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const std::wstring image = to_lower(entry.szExeFile);
            if (image == want || image == want_exe) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool AppLifeCycleUtil::IsNamedPipeExists(const std::wstring& pipe_name) {
    // C# lists \\.\pipe\ and compares names; the canonical Win32 equivalent is
    // probing the pipe directly (works across sessions/ACLs that the C#
    // Directory listing also respects).
    const std::wstring full_path = L"\\\\.\\pipe\\" + pipe_name;
    if (::WaitNamedPipeW(full_path.c_str(), 0)) {
        return true; // pipe exists and is connectable now.
    }
    const DWORD err = ::GetLastError();
    if (err == ERROR_SEM_TIMEOUT) return true;  // exists, busy.
    if (err == ERROR_FILE_NOT_FOUND) return false;
    // ERROR_PIPE_BUSY also indicates existence.
    return err == ERROR_PIPE_BUSY;
}

AppLifeCycleUtil::LivelyAppVer AppLifeCycleUtil::GetRunningLivelyAppVer() {
    if (IsAppMutexRunning(single_instance::kUniqueAppName)) {
        return IsNamedPipeExists(single_instance::GrpcPipeServerName())
                   ? LivelyAppVer::v2
                   : LivelyAppVer::v1;
    }
    return LivelyAppVer::nil;
}

} // namespace lively::common
