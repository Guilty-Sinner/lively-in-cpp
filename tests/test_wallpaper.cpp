// Port of the wallpaper runtime (Lively/Core): the mpv host and the
// IDesktopWallpaper picture wallpaper.
//
// The transcript in tests/goldens/wallpaper_csharp.txt comes from
// `csharp_probe wallpaper` and pins the parts that are pure functions:
//
//   * `mpv.cmd.*`    — the real Newtonsoft serializer, so int/double rendering
//                      (1.5f -> 1.5 but -3f -> -3.0), bool casing and escaping
//                      are genuinely the C# code's output.
//   * `mpv.args.*`   — the 21-fragment mpv command line. Re-expressed verbatim in
//                      the probe (the class lives in the WPF app project, which a
//                      console probe cannot reference), so the port is compared
//                      against the C# *expressions* rather than a hand-written
//                      file the port was built to satisfy.
//   * `mpv.scaler.*` — UpdateScaler's property sequences.
//   * `picture.*`    — the Lively -> Windows DesktopWallpaperPosition mapping.
//
// The window/process half (adopting the child window onto the desktop, killing
// it on failure, waiting for its message loop) is not transcript-verifiable and
// is exercised separately in test_win32.cpp.

#include <catch2/catch_test_macros.hpp>

#include <lively/common/convert.h>
#include <lively/core/mpv.h>
#include <lively/core/picture_wallpaper.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace lively;
using namespace lively::core;

namespace {

#ifdef LIVELY_GOLDENS_DIR
const std::string kGoldensDir = LIVELY_GOLDENS_DIR;
#else
const std::string kGoldensDir = "tests/goldens";
#endif

// The fixture is `label<TAB>value`, LF-terminated (see .gitattributes:
// tests/goldens/** is pinned to LF so a checkout anywhere yields the same bytes).
// A value may contain tabs? No — but it does contain literal `\r\n` escapes and
// embedded quotes, so the split is on the FIRST tab only.
std::map<std::string, std::string> load_transcript() {
    std::ifstream in(kGoldensDir + "/wallpaper_csharp.txt", std::ios::binary);
    REQUIRE(in.good());
    std::map<std::string, std::string> out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos)
            continue;
        out[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return out;
}

// The C# puts Environment.NewLine (CRLF) after every IPC command, because mpv's
// pipe reader is line oriented and the C# app runs on Windows.
std::string escaped(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

struct ArgsCase {
    const char* label;
    MpvLaunchOptions options;
};

std::vector<ArgsCase> arg_cases() {
    const std::string ipc = "mpvsocket<RANDOM>";
    auto base = [] {
        MpvLaunchOptions o;
        o.ipc_server_name = "mpvsocket<RANDOM>";
        o.config_dir = std::nullopt;
        return o;
    };

    std::vector<ArgsCase> cases;

    {
        auto o = base();
        o.type = models::WallpaperType::video;
        o.path = R"(C:\wp\clip.mp4)";
        cases.push_back({"video", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::video;
        o.path = R"(C:\wp\clip.mp4)";
        o.is_windowed = true;
        cases.push_back({"video.windowed", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::video;
        o.path = R"(C:\wp\clip.mp4)";
        o.is_hw_accel = false;
        o.color_space = models::TargetColorspaceHintMode::source;
        o.stream_quality = models::StreamQualitySuggestion::Low;
        cases.push_back({"video.nohw", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::gif;
        o.path = R"(C:\wp\anim.gif)";
        o.color_space = models::TargetColorspaceHintMode::sourceDynamic;
        cases.push_back({"gif", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::videostream;
        o.path = "https://youtu.be/x";
        o.stream_quality = models::StreamQualitySuggestion::Medium;
        cases.push_back({"stream", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::videostream;
        o.path = "https://youtu.be/x";
        o.stream_quality = models::StreamQualitySuggestion::Highest;
        cases.push_back({"stream.high", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::videostream;
        o.path = "https://youtu.be/x";
        o.stream_quality = models::StreamQualitySuggestion::Lowest;
        cases.push_back({"stream.lowest", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::web;
        o.path = "https://example.com/";
        cases.push_back({"web", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::picture;
        o.path = R"(C:\wp\still.jpg)";
        cases.push_back({"picture", o});
    }
    {
        // An embedded quote is NOT escaped by the C# — the path is wrapped in
        // literal quotes and handed to the process as-is.
        auto o = base();
        o.type = models::WallpaperType::video;
        o.path = "C:\\wp\\a b\"c.mp4";
        cases.push_back({"quotes", o});
    }
    {
        auto o = base();
        o.type = models::WallpaperType::video;
        o.path = R"(C:\wp\clip.mp4)";
        o.config_dir = "<BASE>\\plugins\\mpv\\portable_config";
        cases.push_back({"configdir", o});
    }
    (void)ipc;
    return cases;
}

} // namespace

TEST_CASE("mpv launch command lines match the C# transcript", "[wallpaper][oracle]") {
    const auto golden = load_transcript();
    for (const auto& c : arg_cases()) {
        const std::string key = std::string("mpv.args.") + c.label;
        const auto it = golden.find(key);
        INFO("golden line missing: " << key);
        REQUIRE(it != golden.end());
        INFO("case " << c.label);
        CHECK(build_mpv_command_line(c.options) == it->second);
    }
}

TEST_CASE("mpv IPC commands match the C# transcript", "[wallpaper][oracle]") {
    const auto golden = load_transcript();

    struct CmdCase {
        const char* label;
        std::vector<nlohmann::json> parts;
    };

    const std::vector<CmdCase> cases = {
        {"volume.int", {"set_property", "volume", 50}},
        {"volume.zero", {"set_property", "volume", 0}},
        {"pause.true", {"set_property", "pause", true}},
        {"pause.false", {"set_property", "pause", false}},
        // A float and a double are rendered differently by Newtonsoft: 1.5 -> 1.5,
        // 1.0 -> 1.0, and -3f -> -3.0.
        {"slider.float", {"set_property", "speed", 1.5}},
        {"slider.wholefloat", {"set_property", "speed", 1.0}},
        {"slider.int32", {"set_property", "count", 1}},
        {"aid.no", {"set_property", "aid", "no"}},
        {"vid.int", {"set_property", "vid", 1}},
        {"seek.abs", {"seek", 12.5, "absolute-percent"}},
        {"seek.rel", {"seek", -3.0, "relative-percent"}},
        {"name.quoted", {"set_property", "a\"b", "c\\d"}},
        {"screenshot", {"screenshot-to-file", R"(C:\tmp\shot.jpg)"}},
        {"scaler.keepaspect.yes", {"set_property", "keepaspect", "yes"}},
    };

    for (const auto& c : cases) {
        const std::string key = std::string("mpv.cmd.") + c.label;
        const auto it = golden.find(key);
        INFO("golden line missing: " << key);
        REQUIRE(it != golden.end());
        INFO("case " << c.label);
        CHECK(escaped(mpv_ipc_command(c.parts)) == it->second);
    }
}

TEST_CASE("mpv scaler messages match the C# transcript", "[wallpaper][oracle]") {
    const auto golden = load_transcript();

    const std::vector<std::pair<const char*, models::WallpaperScaler>> cases = {
        {"none", models::WallpaperScaler::none},
        {"fill", models::WallpaperScaler::fill},
        {"uniform", models::WallpaperScaler::uniform},
        {"uniformFill", models::WallpaperScaler::uniformFill},
        {"auto", models::WallpaperScaler::autofit},
    };

    for (const auto& [label, scaler] : cases) {
        const std::string key = std::string("mpv.scaler.") + label;
        const auto it = golden.find(key);
        INFO("golden line missing: " << key);
        REQUIRE(it != golden.end());

        std::string joined;
        for (const auto& message : mpv_scaler_messages(scaler)) {
            if (!joined.empty())
                joined += ' ';
            joined += escaped(message);
        }
        INFO("case " << label);
        CHECK(joined == it->second);
    }
}

TEST_CASE("picture wallpaper scaler mapping matches the C# transcript", "[wallpaper][oracle]") {
    const auto golden = load_transcript();

    const std::vector<std::pair<const char*, models::WallpaperScaler>> cases = {
        {"none", models::WallpaperScaler::none},
        {"fill", models::WallpaperScaler::fill},
        {"uniform", models::WallpaperScaler::uniform},
        {"uniformFill", models::WallpaperScaler::uniformFill},
        {"auto", models::WallpaperScaler::autofit},
    };

    for (const auto& [label, scaler] : cases) {
        const std::string key = std::string("picture.scaler.") + label;
        const auto it = golden.find(key);
        INFO("golden line missing: " << key);
        REQUIRE(it != golden.end());

        DesktopWallpaperPosition expected{};
        if (it->second == "Center") expected = DesktopWallpaperPosition::Center;
        else if (it->second == "Tile") expected = DesktopWallpaperPosition::Tile;
        else if (it->second == "Stretch") expected = DesktopWallpaperPosition::Stretch;
        else if (it->second == "Fit") expected = DesktopWallpaperPosition::Fit;
        else if (it->second == "Fill") expected = DesktopWallpaperPosition::Fill;
        else if (it->second == "Span") expected = DesktopWallpaperPosition::Span;

        INFO("case " << label);
        CHECK(picture_scaler_position(scaler) == expected);
    }

    // Show() overrides the scaler for the span arrangement.
    const auto span = golden.find("picture.span");
    REQUIRE(span != golden.end());
    CHECK(span->second == "Span");
    for (const auto scaler : {models::WallpaperScaler::none, models::WallpaperScaler::fill,
                              models::WallpaperScaler::uniform, models::WallpaperScaler::uniformFill,
                              models::WallpaperScaler::autofit}) {
        CHECK(picture_show_position(scaler, models::WallpaperArrangement::span) ==
              DesktopWallpaperPosition::Span);
        CHECK(picture_show_position(scaler, models::WallpaperArrangement::per) ==
              picture_scaler_position(scaler));
    }
}

// ---------------------------------------------------------------------------
// Focused checks for rules the transcript cannot express.

TEST_CASE("Convert.ToInt32 rounds halves to even, not away from zero", "[wallpaper]") {
    using common::convert_to_int32;

    // Away-from-zero (std::lround) would give 3 for the first of these and 4 for
    // the second; .NET gives 2 and 4.
    CHECK(convert_to_int32(2.5) == std::optional<std::int32_t>(2));
    CHECK(convert_to_int32(3.5) == std::optional<std::int32_t>(4));
    CHECK(convert_to_int32(-0.5) == std::optional<std::int32_t>(0));
    CHECK(convert_to_int32(1.4) == std::optional<std::int32_t>(1));
    CHECK(convert_to_int32(1.6) == std::optional<std::int32_t>(2));
    CHECK(convert_to_int32(0.5) == std::optional<std::int32_t>(0));
    CHECK(convert_to_int32(-2.5) == std::optional<std::int32_t>(-2));

    // Convert.ToInt32 throws OverflowException outside the int range; the port
    // reports it instead of aborting a control callback.
    CHECK_FALSE(convert_to_int32(1e300).has_value());
    CHECK_FALSE(convert_to_int32(-1e300).has_value());
    CHECK_FALSE(convert_to_int32(std::numeric_limits<double>::quiet_NaN()).has_value());
    CHECK_FALSE(convert_to_int32(std::numeric_limits<double>::infinity()).has_value());
    CHECK(convert_to_int32(2147483647.0) == std::optional<std::int32_t>(2147483647));
    CHECK(convert_to_int32(-2147483648.0) == std::optional<std::int32_t>(-2147483648));
    CHECK_FALSE(convert_to_int32(2147483648.0).has_value());
}

TEST_CASE("slider step decides int vs double, like the C# LivelyProperties rule", "[wallpaper]") {
    CHECK(common::slider_step_is_fraction(0.5));
    CHECK(common::slider_step_is_fraction(0.25));
    CHECK_FALSE(common::slider_step_is_fraction(1.0));
    CHECK_FALSE(common::slider_step_is_fraction(2.0));
    CHECK_FALSE(common::slider_step_is_fraction(0.0));

    // A whole-number slider is sent as an int (mpv is strongly typed).
    const auto whole = mpv_slider_command("volume", 2.5, 1.0);
    REQUIRE(whole.has_value());
    CHECK(*whole == "{\"command\":[\"set_property\",\"volume\",2]}\r\n");

    // A fractional slider keeps its double value.
    const auto fractional = mpv_slider_command("speed", 1.5, 0.5);
    REQUIRE(fractional.has_value());
    CHECK(*fractional == "{\"command\":[\"set_property\",\"speed\",1.5]}\r\n");

    // The C# catches OverflowException around the whole conversion, so the port
    // returns "no command" rather than a garbage integer.
    CHECK_FALSE(mpv_slider_command("volume", 1e300, 1.0).has_value());
}

TEST_CASE("every mpv IPC command is CRLF terminated", "[wallpaper]") {
    // Environment.NewLine on Windows is CRLF and mpv's reader is line oriented —
    // an LF-only command is silently ignored, which looks like "controls do
    // nothing" rather than a protocol error.
    const std::string command = mpv_set_property("pause", true);
    REQUIRE(command.size() >= 2);
    CHECK(command.substr(command.size() - 2) == "\r\n");

    const std::string seek = mpv_seek(0.0, "absolute-percent");
    CHECK(seek.substr(seek.size() - 2) == "\r\n");
    CHECK(seek == "{\"command\":[\"seek\",0.0,\"absolute-percent\"]}\r\n");
}

TEST_CASE("mpv config dir discovery follows the C# priority order", "[wallpaper]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "lively_mpv_cfg_test";
    std::error_code ec;
    fs::remove_all(root, ec);

    // Nothing exists: GetConfigDir returns null and the launcher passes
    // --no-config so the user's %APPDATA%\mpv\mpv.conf cannot leak in.
    const auto none = find_mpv_config_dir(root.string());
    CHECK_FALSE(none.has_value());

    // The portable dir exists under the app base directory.
    const fs::path portable = root / "plugins" / "mpv" / "portable_config";
    fs::create_directories(portable, ec);
    REQUIRE_FALSE(ec);
    const auto found = find_mpv_config_dir(root.string());
    REQUIRE(found.has_value());
    CHECK(*found == portable.string());

    fs::remove_all(root, ec);
}

TEST_CASE("mpv exit codes map to the C# exception types", "[wallpaper]") {
    // GetMpvException: 1 = unknown options / init failure, 2 and 3 = unplayable
    // file, anything else = the generic resource string shown in the UI.
    CHECK(classify_mpv_exit(1).kind == "plugin");
    CHECK(classify_mpv_exit(2).kind == "file");
    CHECK(classify_mpv_exit(3).kind == "file");
    CHECK(classify_mpv_exit(0).kind == "general");
    CHECK(classify_mpv_exit(4).kind == "general");
    CHECK(classify_mpv_exit(1).message.find("Error initializing mpv") != std::string::npos);
    CHECK(classify_mpv_exit(2).message.find("couldn't be played") != std::string::npos);
    CHECK(classify_mpv_exit(9).message.find("Oops... Looks like something went wrong") != std::string::npos);
}
