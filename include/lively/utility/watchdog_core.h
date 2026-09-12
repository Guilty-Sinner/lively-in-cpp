#pragma once
// Port of Lively.Utility.Watchdog — kills external-application wallpapers when
// the Lively main process dies.
//
// Reference C#: Lively.Utility.Watchdog/Program.cs
//   stdin commands (one per line): ADD/RMV {pid} | CLR
//   On parent exit: kill every watched pid, then every process named
//   "Lively.UI.WinUI", then force a desktop refresh (SPI_SETDESKWALLPAPER).
//
// Parsing semantics matched exactly:
//   * verbs compare OrdinalIgnoreCase ("CLR"/"clr"/"ClR" all match)
//   * ADD/RMV require args[1] to parse as an int (invariant culture), else ignore
//   * RMV removes at most one entry (List.Remove), ADD allows duplicates
//   * CLR clears the list
//   * short input ("ADD" with no pid) -> args[1] access throws in C# (caught by
//     the outer catch); the C++ port treats it as no-op.
//
// The protocol core is platform-independent so it can be unit-tested; the
// process-kill/SPI part lives in main.cpp (Win32).

#include <string>
#include <vector>

namespace lively::utility {

class WatchdogWatchlist {
public:
    // Handles one stdin line; mutates the watchlist exactly like the C# program.
    void HandleLine(const std::string& line);

    const std::vector<int>& items() const noexcept { return programs_; }

private:
    std::vector<int> programs_;
};

// Parses "ADD 42"/"RMV 42"/"CLR" the way C# string.Split(' ') +
// int.TryParse would. Returns: 0 = none, 1 = add, 2 = remove, 3 = clear.
enum class WatchdogCommand { none, add, remove, clear };

WatchdogCommand ParseWatchdogCommand(const std::string& line, int& pid_out);

} // namespace lively::utility
