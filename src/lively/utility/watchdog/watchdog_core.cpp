#include "lively/utility/watchdog_core.h"

#include <algorithm>

namespace lively::utility {

namespace {
std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') c = static_cast<char>(u - 'A' + 'a');
    }
    return s;
}

// int.TryParse equivalent: optional sign, decimal digits only, fits in int.
bool try_parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        i = 1;
        if (s.size() == 1) return false;
    }
    long long value = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (value > 2147483648LL) return false; // overflow -> parse fail (like .NET)
    }
    out = negative ? static_cast<int>(-value) : static_cast<int>(value);
    return true;
}
} // namespace

WatchdogCommand ParseWatchdogCommand(const std::string& line, int& pid_out) {
    // C#: msg.Split(' ') — empty tokens are preserved; we replicate the
    // observable behaviour for the verb/pid tokens.
    std::vector<std::string> args;
    std::string current;
    for (const char c : line) {
        if (c == ' ') {
            args.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    args.push_back(current);

    if (args.empty()) return WatchdogCommand::none;
    const std::string verb = to_lower_ascii(args[0]);

    if (verb == "clr") return WatchdogCommand::clear;

    int pid = 0;
    if ((verb == "add" || verb == "rmv") && args.size() > 1 && try_parse_int(args[1], pid)) {
        pid_out = pid;
        return verb == "add" ? WatchdogCommand::add : WatchdogCommand::remove;
    }
    return WatchdogCommand::none;
}

void WatchdogWatchlist::HandleLine(const std::string& line) {
    int pid = 0;
    switch (ParseWatchdogCommand(line, pid)) {
        case WatchdogCommand::clear:
            programs_.clear();
            break;
        case WatchdogCommand::add:
            programs_.push_back(pid);
            break;
        case WatchdogCommand::remove: {
            // C# List.Remove: removes first occurrence only.
            const auto it = std::find(programs_.begin(), programs_.end(), pid);
            if (it != programs_.end()) programs_.erase(it);
            break;
        }
        case WatchdogCommand::none:
            break;
    }
}

} // namespace lively::utility
