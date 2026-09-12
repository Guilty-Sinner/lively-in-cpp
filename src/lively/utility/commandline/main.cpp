// Port of Lively.Utility.Commandline/Program.cs.
//
// C# behaviour:
//  1. Parse argv against the verb model (help text on parse errors).
//  2. If the Lively core mutex ("LIVELY:DESKTOPWALLPAPERSYSTEM") is NOT running,
//     print "\nWARNING: Lively core is currently not running!" and exit.
//  3. Otherwise forward the raw args to the core via gRPC (AutomationCommand).
//
// The mutex check maps to OpenMutexW; the gRPC forwarding is a Phase-2 stub
// (see docs/phase2-port-plan.md) that prints the parsed intent so the CLI is
// testable without the core.

#include "lively/utility/commandline.h"
#include "lively/utility/single_instance.h"

#include <iostream>

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.reserve(argc > 1 ? argc - 1 : 0);
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    const auto result = lively::utility::ParseCommandLine(args);
    if (!result.success) {
        // C# CommandLineParser prints usage; keep the failure silent-but-nonzero
        // for scripting parity until the help generator is ported.
        for (const auto& e : result.errors) std::cerr << e << "\n";
        return 1;
    }

    if (!lively::utility::IsAppMutexRunning(L"LIVELY:DESKTOPWALLPAPERSYSTEM")) {
        std::cout << "\nWARNING: Lively core is currently not running!";
        return 0;
    }

    // Phase 2: CommandsClient.AutomationCommand(args) over gRPC.
    std::cout << "livelycmd: parsed OK; gRPC forwarding pending (phase 2)\n";
    return 0;
}
