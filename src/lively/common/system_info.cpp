#include "lively/common/system_info.h"

#include <windows.h>

#include <sstream>
#include <vector>

namespace lively::common {

namespace {

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::string read_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, buffer.data(), &size) !=
        ERROR_SUCCESS) {
        return {};
    }
    buffer.resize(buffer.find(L'\0') == std::wstring::npos ? buffer.size()
                                                           : buffer.find(L'\0'));
    return wide_to_utf8(buffer);
}

// WMI Win32_VideoController.Name enumerates every GPU adapter.
std::vector<std::string> enumerate_gpus() {
    std::vector<std::string> gpus;
    // Current adapter (highest-numbered key is the primary; enumerate all).
    for (int i = 0;; ++i) {
        const std::wstring key =
            L"SOFTWARE\\Microsoft\\DirectX\\AdapterMan\\graphics\\adapters\\" + std::to_wstring(i);
        const std::string name = read_registry_string(HKEY_LOCAL_MACHINE, key.c_str(), L"Description");
        if (name.empty()) break;
        gpus.push_back(name);
    }
    if (!gpus.empty()) return gpus;
    // Fallback: classic display class enumeration (matches WMI output closely).
    for (int i = 0;; ++i) {
        const std::wstring key =
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\"
            L"000" + std::to_wstring(i % 10);
        const std::string name = read_registry_string(HKEY_LOCAL_MACHINE, key.c_str(), L"DriverDesc");
        if (name.empty()) break;
        gpus.push_back(name);
        if (i >= 9) break;
    }
    return gpus;
}

// WMI Win32_Processor.Name (single socket typical).
std::string read_cpu_name() {
    return read_registry_string(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
}

// WMI Win32_OperatingSystem: Caption (product name) + Version.
std::string read_os_caption() {
    return read_registry_string(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
}

std::string read_os_version() {
    return read_registry_string(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentVersion");
}

template <typename Range>
std::string join_lines(const std::string& prefix, const Range& items) {
    // Mirrors sb.AppendLine(prefix + item) then TrimEnd(): "\n"-joined,
    // no trailing newline.
    std::ostringstream out;
    bool first = true;
    for (const auto& item : items) {
        if (!first) out << "\n";
        out << prefix << item;
        first = false;
    }
    return out.str();
}

} // namespace

std::string SystemInfo::GetGpuInfo() {
    // C# wraps in try/catch: on failure returns "GPU: <message>". Here, an
    // empty enumeration yields the equivalent error line shape.
    const auto gpus = enumerate_gpus();
    if (gpus.empty()) return "GPU: not found";
    return join_lines("GPU: ", gpus);
}

std::vector<std::string> SystemInfo::GetGpu() { return enumerate_gpus(); }

std::string SystemInfo::GetCpuInfo() {
    const std::string cpu = read_cpu_name();
    if (cpu.empty()) return "CPU: not found";
    return "CPU: " + cpu;
}

std::vector<std::string> SystemInfo::GetCpu() {
    const std::string cpu = read_cpu_name();
    if (cpu.empty()) return {};
    return {cpu};
}

std::string SystemInfo::GetOSInfo() {
    const std::string caption = read_os_caption();
    if (caption.empty()) return "OS: not found";
    return "OS: " + caption + " " + read_os_version();
}

} // namespace lively::common
