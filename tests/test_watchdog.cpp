#include <catch2/catch_test_macros.hpp>

#include <lively/utility/watchdog_core.h>

using namespace lively::utility;

TEST_CASE("watchdog protocol ADD/RMV/CLR", "[watchdog]") {
    WatchdogWatchlist w;

    w.HandleLine("ADD 100");
    w.HandleLine("ADD 200");
    REQUIRE(w.items() == (std::vector<int>{100, 200}));

    // RMV removes a single occurrence.
    w.HandleLine("RMV 100");
    REQUIRE(w.items() == (std::vector<int>{200}));

    // Unknown / malformed lines are ignored (C# swallows via try/catch).
    w.HandleLine("ADD");      // missing pid -> no-op
    w.HandleLine("ADD abc");  // unparseable pid -> no-op
    w.HandleLine("FOO 1");
    w.HandleLine("");
    REQUIRE(w.items() == (std::vector<int>{200}));

    // Verb comparison is OrdinalIgnoreCase.
    w.HandleLine("add 300");
    REQUIRE(w.items() == (std::vector<int>{200, 300}));
    w.HandleLine("rMv 200");
    REQUIRE(w.items() == (std::vector<int>{300}));

    // CLR clears the list.
    w.HandleLine("CLR");
    REQUIRE(w.items().empty());
}

TEST_CASE("watchdog command classification", "[watchdog]") {
    int pid = 0;
    REQUIRE(ParseWatchdogCommand("ADD 42", pid) == WatchdogCommand::add);
    REQUIRE(pid == 42);
    REQUIRE(ParseWatchdogCommand("clr", pid) == WatchdogCommand::clear);
    REQUIRE(ParseWatchdogCommand("RMV 7", pid) == WatchdogCommand::remove);
    REQUIRE(pid == 7);
    REQUIRE(ParseWatchdogCommand("junk", pid) == WatchdogCommand::none);
    // Negative pids parse like int.TryParse.
    REQUIRE(ParseWatchdogCommand("ADD -5", pid) == WatchdogCommand::add);
    REQUIRE(pid == -5);
}
