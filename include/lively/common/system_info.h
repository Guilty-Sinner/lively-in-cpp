#pragma once
// Port of Lively.Common/Helpers/Hardware/SystemInfo.cs.
//
// C# uses System.Management (WMI: Win32_VideoController, Win32_Processor,
// Win32_OperatingSystem). The C++ port uses the Win32 APIs that feed the same
// WMI tables (they are wrappers over these), preserving the exact output
// prefixes/formats: "GPU: <name>", "CPU: <name>", "OS: <caption> <version>".

#include <string>
#include <vector>

namespace lively::common {

class SystemInfo {
public:
    // "GPU: name" lines, TrimEnd'd — identical to GetGpuInfo().
    static std::string GetGpuInfo();
    static std::vector<std::string> GetGpu();

    // "CPU: name" lines — identical to GetCpuInfo().
    static std::string GetCpuInfo();
    static std::vector<std::string> GetCpu();

    // "OS: caption version" — identical to GetOSInfo().
    static std::string GetOSInfo();
};

} // namespace lively::common
