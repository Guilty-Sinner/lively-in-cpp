#pragma once
// Port of Lively.Common/Helpers/AppLifeCycleUtil.cs.

#include <string>

#include "lively/common/constants.h"

namespace lively::common {

class AppLifeCycleUtil {
public:
    // Mutex.TryOpenExisting + dispose. (Already used by lively_cmd.)
    static bool IsAppMutexRunning(const std::wstring& mutex_name);

    // Process.GetProcessesByName(name).Count() != 0
    static bool IsAppProcessRunning(const std::wstring& process_name);

    // Directory.GetFiles(@"\\.\pipe\") exists-check equivalent via
    // WaitNamedPipe(NMPWAIT_USE_DEFAULT_WAIT) / CreateFile probe.
    static bool IsNamedPipeExists(const std::wstring& pipe_name);

    enum class LivelyAppVer { nil = 0, v1 = 1, v2 = 2 };

    // Mutex running + gRPC pipe present => v2; mutex + no pipe => v1; else nil.
    static LivelyAppVer GetRunningLivelyAppVer();
};

} // namespace lively::common
