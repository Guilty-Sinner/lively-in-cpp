// Port of Lively.Player.{Wmf,Vlc,WebView2,CefSharp}/StartArgs.cs.
//
// Two layers of verification:
//   1. unit cases whose expected strings were captured from the real
//      CommandLineParser 2.9.1 (see tools/csharp_probe/StartArgsProbe.cs);
//   2. a differential run — a deterministic adversarial corpus is fed to BOTH
//      the C# oracle (`csharp_probe players fuzz`) and the C++ port, and the
//      canonical outcome lines must match exactly.

#include <catch2/catch_test_macros.hpp>

#include <lively/players/start_args.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace lively::players;

namespace {

// ---- canonical description (must match StartArgsProbe.Describe byte-for-byte) ----

std::string txt(const std::optional<std::string>& v) {
    return v ? "[" + *v + "]" : "<null>";
}

std::string txt(const std::string& v) { return "[" + v + "]"; }

std::string num(int v) { return std::to_string(v); }

std::string bool_str(bool v) { return v ? "true" : "false"; }

// .NET "R"/shortest-round-trip formatting: std::to_chars also emits the shortest
// representation that round-trips; only the exponent marker differs (E vs e).
std::string opt_num(const std::optional<double>& v) {
    if (!v) return "<null>";
    const double d = *v;
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), d);
    std::string s(buffer, result.ptr);
    std::replace(s.begin(), s.end(), 'e', 'E');
    return s;
}

std::string describe(const WmfStartArgs& a) {
    return "OK wmf"
        + (std::string(";path=") + txt(a.file_path))
        + ";stretch=" + num(a.stretch_mode)
        + ";volume=" + num(a.volume)
        + ";property=" + txt(a.properties)
        + ";verbose-log=" + bool_str(a.verbose_log);
}

std::string describe(const VlcStartArgs& a) {
    return "OK vlc"
        + (std::string(";wallpaper-path=") + txt(a.file_path))
        + ";wallpaper-volume=" + num(a.volume)
        + ";wallpaper-hardware-decoding=" + bool_str(a.hardware_decoding)
        + ";wallpaper-property=" + txt(a.properties)
        + ";wallpaper-geometry=" + txt(a.geometry)
        + ";wallpaper-color-scheme=" + num(static_cast<int>(a.theme))
        + ";wallpaper-verbose-log=" + bool_str(a.verbose_log);
}

std::string describe(const WebView2StartArgs& a) {
    return "OK webview2"
        + (std::string(";wallpaper-url=") + txt(a.url))
        + ";wallpaper-property=" + txt(a.properties)
        + ";wallpaper-type=" + num(static_cast<int>(a.type))
        + ";wallpaper-display=" + txt(a.display_device)
        + ";wallpaper-geometry=" + txt(a.geometry)
        + ";wallpaper-scale=" + opt_num(a.scale)
        + ";wallpaper-audio=" + bool_str(a.audio_visualizer)
        + ";wallpaper-audio-id=" + txt(a.audio_visualizer_device_id)
        + ";wallpaper-debug=" + txt(a.debug_port)
        + ";wallpaper-user-data=" + txt(a.user_data_path)
        + ";wallpaper-volume=" + num(a.volume)
        + ";wallpaper-system-information=" + bool_str(a.sys_info)
        + ";wallpaper-system-nowplaying=" + bool_str(a.now_playing)
        + ";wallpaper-pause-event=" + bool_str(a.pause_event)
        + ";wallpaper-pause-media=" + bool_str(a.pause_web_media)
        + ";wallpaper-verbose-log=" + bool_str(a.verbose_log)
        + ";wallpaper-color-scheme=" + num(static_cast<int>(a.theme));
}

std::string describe(const CefSharpStartArgs& a) {
    return "OK cef"
        + (std::string(";wallpaper-url=") + txt(a.url))
        + ";wallpaper-property=" + txt(a.properties)
        + ";wallpaper-type=" + num(static_cast<int>(a.type))
        + ";wallpaper-display=" + txt(a.display_device)
        + ";wallpaper-geometry=" + txt(a.geometry)
        + ";wallpaper-audio=" + bool_str(a.audio_visualizer)
        + ";wallpaper-audio-id=" + txt(a.audio_visualizer_device_id)
        + ";wallpaper-debug=" + txt(a.debug_port)
        + ";wallpaper-cache=" + txt(a.cache_path)
        + ";wallpaper-volume=" + num(a.volume)
        + ";wallpaper-system-information=" + bool_str(a.sys_info)
        + ";wallpaper-system-nowplaying=" + bool_str(a.now_playing)
        + ";wallpaper-pause-event=" + bool_str(a.pause_event)
        + ";wallpaper-verbose-log=" + bool_str(a.verbose_log)
        + ";wallpaper-color-scheme=" + num(static_cast<int>(a.theme));
}

// Player name -> canonical outcome line (mirrors StartArgsProbe.ParseOne).
std::string outcome(const std::string& player, const std::vector<std::string>& args) {
    if (player == "wmf") {
        const auto r = ParseWmfStartArgs(args);
        return r.success ? describe(r.args) : "ERR " + r.error;
    }
    if (player == "vlc") {
        const auto r = ParseVlcStartArgs(args);
        return r.success ? describe(r.args) : "ERR " + r.error;
    }
    if (player == "webview2") {
        const auto r = ParseWebView2StartArgs(args);
        return r.success ? describe(r.args) : "ERR " + r.error;
    }
    if (player == "cef") {
        const auto r = ParseCefSharpStartArgs(args);
        return r.success ? describe(r.args) : "ERR " + r.error;
    }
    return "ERR unknown-player";
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Deterministic adversarial token pool: valid names, unknown names, malformed
// forms ("--", "--=", "-5", "-"), and values covering every conversion edge.
const std::vector<std::string>& token_pool() {
    static const std::vector<std::string> pool = {
        // valid option names (unioned across the four players)
        "--path", "--stretch", "--volume", "--property", "--verbose-log",
        "--wallpaper-path", "--wallpaper-volume", "--wallpaper-hardware-decoding",
        "--wallpaper-property", "--wallpaper-geometry", "--wallpaper-color-scheme",
        "--wallpaper-verbose-log", "--wallpaper-url", "--wallpaper-type",
        "--wallpaper-display", "--wallpaper-scale", "--wallpaper-audio",
        "--wallpaper-audio-id", "--wallpaper-debug", "--wallpaper-user-data",
        "--wallpaper-system-information", "--wallpaper-system-nowplaying",
        "--wallpaper-pause-event", "--wallpaper-pause-media", "--wallpaper-cache",
        // unknown / short / malformed
        "--nope", "--x", "-v", "-5", "-", "--", "--=", "--volume=", "--volume=x",
        "--verbose-log=false", "--wallpaper-hardware-decoding=false",
        "--wallpaper-type=local",
        // values
        "0", "1", "55", "-3", "100", "2147483647", "2147483648", "1.5", "1,5",
        "1e3", "NaN", "Infinity", "-Infinity", "abc", "true", "false", "TRUE",
        "Auto", "Light", "Dark", "dark", "online", "local", "Online", "bogus",
        "99", "a.mp4", "x",
    };
    return pool;
}

// Same LCG on any platform — the corpus must be identical for both sides.
struct Lcg {
    uint64_t state;
    uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state >> 33);
    }
};

struct CorpusCase {
    std::string player;
    std::vector<std::string> args;
    std::string line;  // "<player> <args...>" as fed to the C# oracle
};

std::vector<CorpusCase> build_corpus(std::size_t count, uint64_t seed) {
    static const char* kPlayers[] = {"wmf", "vlc", "webview2", "cef"};
    const auto& pool = token_pool();
    Lcg rng{seed};

    std::vector<CorpusCase> cases;
    cases.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        CorpusCase c;
        c.player = kPlayers[rng.next() % 4];
        const std::size_t tokens = rng.next() % 9;
        std::string line = c.player;
        for (std::size_t t = 0; t < tokens; ++t) {
            const std::string& token = pool[rng.next() % pool.size()];
            c.args.push_back(token);
            line += " ";
            line += token;
        }
        c.line = line;
        cases.push_back(std::move(c));
    }
    return cases;
}

} // namespace

TEST_CASE("player start-args defaults match the C# Default= attributes", "[players]") {
    SECTION("wmf requires --path and defaults the rest") {
        const auto r = ParseWmfStartArgs({"--path", "a.mp4"});
        REQUIRE(r.success);
        CHECK(r.args.file_path == "a.mp4");
        CHECK(r.args.stretch_mode == 0);
        CHECK(r.args.volume == 100);
        CHECK_FALSE(r.args.properties.has_value());
        CHECK_FALSE(r.args.verbose_log);
    }

    SECTION("vlc hardware decoding defaults to true") {
        const auto r = ParseVlcStartArgs({"--wallpaper-path", "a.mp4"});
        REQUIRE(r.success);
        CHECK(r.args.volume == 100);
        CHECK(r.args.hardware_decoding);
        CHECK(r.args.theme == lively::models::AppTheme::Auto);
    }

    SECTION("webview2/cet defaults") {
        const auto wv = ParseWebView2StartArgs({"--wallpaper-url", "x", "--wallpaper-type", "local"});
        REQUIRE(wv.success);
        CHECK(wv.args.volume == 100);
        CHECK_FALSE(wv.args.scale.has_value());
        CHECK_FALSE(wv.args.audio_visualizer);
        CHECK(wv.args.type == lively::models::WebPageType::local);

        const auto cef = ParseCefSharpStartArgs(
            {"--wallpaper-url", "x", "--wallpaper-type", "online", "--wallpaper-display", "dev"});
        REQUIRE(cef.success);
        CHECK(cef.args.display_device == "dev");
        CHECK(cef.args.volume == 100);
    }
}

TEST_CASE("player start-args reproduce CommandLineParser quirks", "[players]") {
    SECTION("bool is a switch: presence means true and the value is ignored") {
        // Oracle: `vlc --wallpaper-path a --wallpaper-hardware-decoding false`
        // yields True — the switch does not read the following value.
        const auto spaced = ParseVlcStartArgs(
            {"--wallpaper-path", "a", "--wallpaper-hardware-decoding", "false"});
        REQUIRE(spaced.success);
        CHECK(spaced.args.hardware_decoding);

        const auto equals = ParseVlcStartArgs(
            {"--wallpaper-path", "a", "--wallpaper-hardware-decoding=false"});
        REQUIRE(equals.success);
        CHECK(equals.args.hardware_decoding);
    }

    SECTION("a switch followed by another option does not swallow it") {
        const auto r = ParseWmfStartArgs({"--path", "a.mp4", "--verbose-log", "--volume", "55"});
        REQUIRE(r.success);
        CHECK(r.args.verbose_log);
        CHECK(r.args.volume == 55);
    }

    SECTION("enums are matched case-sensitively") {
        const auto exact = ParseVlcStartArgs({"--wallpaper-path", "a", "--wallpaper-color-scheme", "Dark"});
        REQUIRE(exact.success);
        CHECK(exact.args.theme == lively::models::AppTheme::Dark);

        const auto lower = ParseVlcStartArgs({"--wallpaper-path", "a", "--wallpaper-color-scheme", "dark"});
        CHECK_FALSE(lower.success);
        CHECK(lower.error == "BadFormatConversionError");
    }

    SECTION("enum ordinals are accepted, undefined ones are rejected") {
        const auto one = ParseWebView2StartArgs({"--wallpaper-url", "x", "--wallpaper-type", "1"});
        REQUIRE(one.success);
        CHECK(one.args.type == lively::models::WebPageType::local);

        const auto ninety_nine = ParseWebView2StartArgs({"--wallpaper-url", "x", "--wallpaper-type", "99"});
        CHECK_FALSE(ninety_nine.success);
        // The failed conversion leaves the required option unbound.
        CHECK(ninety_nine.error == "BadFormatConversionError,MissingRequiredOptionError");
    }

    SECTION("invariant double: groups ignored, NaN accepted") {
        const auto grouped = ParseWebView2StartArgs(
            {"--wallpaper-url", "x", "--wallpaper-type", "local", "--wallpaper-scale", "1,5"});
        REQUIRE(grouped.success);
        REQUIRE(grouped.args.scale.has_value());
        CHECK(*grouped.args.scale == 15.0);

        const auto exponent = ParseWebView2StartArgs(
            {"--wallpaper-url", "x", "--wallpaper-type", "local", "--wallpaper-scale", "1e3"});
        REQUIRE(exponent.success);
        REQUIRE(exponent.args.scale.has_value());
        CHECK(*exponent.args.scale == 1000.0);

        const auto nan = ParseWebView2StartArgs(
            {"--wallpaper-url", "x", "--wallpaper-type", "local", "--wallpaper-scale", "NaN"});
        REQUIRE(nan.success);
        REQUIRE(nan.args.scale.has_value());
        CHECK(std::isnan(*nan.args.scale));
    }

    SECTION("required/repeated/unknown errors") {
        const auto missing = ParseWmfStartArgs({"--volume", "5"});
        CHECK_FALSE(missing.success);
        CHECK(missing.error == "MissingRequiredOptionError");

        const auto repeated = ParseWmfStartArgs({"--path", "a", "--path", "b"});
        CHECK_FALSE(repeated.success);
        CHECK(repeated.error == "RepeatedOptionError");

        const auto unknown = ParseWmfStartArgs({"--path", "a", "--nope", "1"});
        CHECK_FALSE(unknown.success);
        CHECK(unknown.error == "UnknownOptionError:nope");

        const auto help = ParseWmfStartArgs({"--help"});
        CHECK_FALSE(help.success);
        CHECK(help.error == "HelpRequestedError");

        const auto bad_format = ParseWmfStartArgs({"--path="});
        CHECK_FALSE(bad_format.success);
        CHECK(bad_format.error == "BadFormatTokenError,MissingRequiredOptionError");
    }

    SECTION("an unpaired scalar name leaves the default in place") {
        // "--volume" with no value never binds, so Volume keeps Default = 100.
        const auto r = ParseWmfStartArgs({"--path", "a", "--volume"});
        REQUIRE(r.success);
        CHECK(r.args.volume == 100);
    }
}

TEST_CASE("player start-args match the real C# CommandLineParser", "[.players-oracle]") {
    if (!file_exists(LIVELY_PROBE_EXE)) {
        FAIL("csharp_probe executable not found at " LIVELY_PROBE_EXE
             "; build it first: dotnet build tools/csharp_probe -c Release");
    }

    const std::string work = std::string(LIVELY_CROSSLANG_DIR);
    std::filesystem::create_directories(work);
    const std::string cases_path = work + "/players_cases.txt";
    const std::string out_path = work + "/players_out.txt";
    std::remove(cases_path.c_str());
    std::remove(out_path.c_str());

    const auto cases = build_corpus(1500, 0x5eed1234ULL);
    {
        std::ofstream file(cases_path, std::ios::binary);
        REQUIRE(file.good());
        for (const auto& c : cases) file << c.line << "\n";
    }

    // Run the oracle: "<line>\t<outcome>" per case.
    // 2>nul: CommandLineParser's Parser.Default writes its auto-generated usage
    // text to stderr on every failed parse — noise we do not want in test output.
    const std::string command = "cmd /c \"\"" LIVELY_PROBE_EXE "\" players fuzz \"" + cases_path +
                                "\" > \"" + out_path + "\" 2>nul\"";
    REQUIRE(std::system(command.c_str()) == 0);

    std::ifstream out(out_path, std::ios::binary);
    REQUIRE(out.good());

    std::size_t compared = 0;
    std::size_t mismatches = 0;
    std::string line;
    for (std::size_t i = 0; i < cases.size() && std::getline(out, line); ++i) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto tab = line.rfind('\t');
        REQUIRE(tab != std::string::npos);
        const std::string expected = line.substr(tab + 1);
        const std::string actual = outcome(cases[i].player, cases[i].args);
        ++compared;
        if (expected != actual) {
            if (++mismatches <= 10) {
                INFO("case " << i << ": " << cases[i].line);
                INFO("C# oracle : " << expected);
                INFO("C++ port  : " << actual);
                CHECK(expected == actual);
            }
        }
    }

    CHECK(compared == cases.size());
    CHECK(mismatches == 0);
}
