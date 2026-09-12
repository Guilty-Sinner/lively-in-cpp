#pragma once
// Port of Lively.Common/Helpers/AppLifeCycleUtil.IsAppMutexRunning +
// Constants.SingleInstance (mutex name "LIVELY:DESKTOPWALLPAPERSYSTEM").

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>

namespace lively::utility {

// Constants.SingleInstance.UniqueAppName
inline constexpr wchar_t kLivelyMutexName[] = L"LIVELY:DESKTOPWALLPAPERSYSTEM";

// Returns true if another process owns the named mutex (C# Mutex try-open).
inline bool IsAppMutexRunning(const wchar_t* mutex_name) {
#ifdef _WIN32
    HANDLE mutex = ::OpenMutexW(SYNCHRONIZE, FALSE, mutex_name);
    if (mutex) {
        ::CloseHandle(mutex);
        return true;
    }
    return false;
#else
    (void)mutex_name;
    return false; // non-Windows: no core, matches "not running"
#endif
}

} // namespace lively::utility
