// Port of Lively.Utility.Watchdog/Program.cs (Win32 side).
//
// C# behaviour:
//   1. args[0] must parse as the parent pid, else exit silently.
//   2. Attach to the parent process; exit if it cannot be opened.
//   3. Start the stdin listener (ADD/RMV/CLR protocol).
//   4. Block until the parent exits.
//   5. Kill every watched pid, then every "Lively.UI.WinUI" process.
//   6. Force desktop refresh: SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, NULL,
//      SPIF_UPDATEINIFILE).

#include "lively/utility/watchdog_core.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cwctype>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

std::optional<int> ParsePid(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        const int value = std::stoi(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

// Enumerates process ids matching a case-insensitive image name (no ".exe"
// suffix required), mirroring Process.GetProcessesByName.
std::vector<int> GetProcessesByName(const std::wstring& name_no_ext) {
    std::vector<int> pids;
    const std::wstring exe = name_no_ext + L".exe";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::wstring exe_name = entry.szExeFile;
            // Case-insensitive compare, allow both "Name" and "Name.exe".
            std::wstring lower = exe_name;
            for (wchar_t& c : lower) c = static_cast<wchar_t>(::towlower(c));
            std::wstring want = exe;
            for (wchar_t& c : want) c = static_cast<wchar_t>(::towlower(c));
            std::wstring want2 = name_no_ext;
            for (wchar_t& c : want2) c = static_cast<wchar_t>(::towlower(c));
            if (lower == want || lower == want2) {
                pids.push_back(static_cast<int>(entry.th32ProcessID));
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}

bool KillProcess(int pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const BOOL ok = TerminateProcess(h, 1);
    // C# Process.Kill is asynchronous; WaitForExit is not called, so we mirror
    // by not waiting here.
    CloseHandle(h);
    return ok != FALSE;
}

void KillAllById(const std::vector<int>& pids) {
    for (const int pid : pids) {
        KillProcess(pid); // C# wraps in try/catch { } — ignore failures.
    }
}

// StdInListener port: reads lines forever (until stdin closes) applying the
// protocol to the shared watchlist. C# fires this as fire-and-forget task; a
// detached thread is the closest synchronous-lifetime equivalent.
void StartStdInListener(lively::utility::WatchdogWatchlist& watchlist) {
    std::thread([&watchlist] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            watchlist.HandleLine(line);
        }
    }).detach();
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        // C#: "Incorrect no of arguments." -> silent return.
        return 0;
    }
    const std::optional<int> parent_pid = ParsePid(argv[1]);
    if (!parent_pid) {
        return 0; // C#: conversion failure -> return.
    }

    // Process.GetProcessById: throws if not found -> silent return.
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(*parent_pid));
    if (!parent) {
        return 0;
    }

    lively::utility::WatchdogWatchlist watchlist;
    StartStdInListener(watchlist);

    // Block until parent exits.
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);

    KillAllById(watchlist.items());
    KillAllById(GetProcessesByName(L"Lively.UI.WinUI"));

    // Force desktop refresh (C# SystemParametersInfo P/Invoke).
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_UPDATEINIFILE);
    return 0;
}
