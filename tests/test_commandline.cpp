#include <catch2/catch_test_macros.hpp>

#include <lively/utility/commandline.h>

#include <string>
#include <vector>

using namespace lively::utility;

namespace {
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ' ') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}
ParseResult parse(const std::string& line) { return ParseCommandLine(split(line)); }
} // namespace

TEST_CASE("app verb parses optional flags", "[cmdline]") {
    const auto r = parse("app --showApp true --volume +10");
    REQUIRE(r.success);
    const auto& o = std::get<AppOptions>(r.options);
    REQUIRE(o.show_app.value_or(false));
    REQUIRE(o.volume.value_or("") == "+10");
}

TEST_CASE("bare verb defaults to app", "[cmdline]") {
    const auto r = parse("--shutdown true");
    REQUIRE(r.success);
    REQUIRE(std::holds_alternative<AppOptions>(r.options));
    const auto& o = std::get<AppOptions>(r.options);
    REQUIRE(o.shutdown_app.value_or(false));
}

TEST_CASE("setwp requires file and detects special values", "[cmdline]") {
    auto r = parse("setwp"); // missing required --file
    REQUIRE_FALSE(r.success);

    r = parse("setwp --file random");
    REQUIRE(r.success);
    const auto& o = std::get<SetWallpaperOptions>(r.options);
    REQUIRE(o.is_random());
    REQUIRE_FALSE(o.is_reload());

    r = parse("setwp --file C:\\wallpapers\\demo --monitor 1");
    REQUIRE(r.success);
    const auto& o2 = std::get<SetWallpaperOptions>(r.options);
    REQUIRE(o2.file == "C:\\wallpapers\\demo");
    REQUIRE(o2.monitor.value_or(-1) == 1);
}

TEST_CASE("closewp requires monitor", "[cmdline]") {
    REQUIRE_FALSE(parse("closewp").success);
    // Negative numbers: CommandLineParser's tokenizer treats "-<digit>" as a
    // Value token (not a short option), so it binds as monitor=-1.
    const auto r = parse("closewp --monitor -1");
    REQUIRE(r.success);
    REQUIRE(std::get<CloseWallpaperOptions>(r.options).monitor == -1);
}

TEST_CASE("screensaver options", "[cmdline]") {
    const auto r = parse("screensaver --show true --fadeIn false");
    REQUIRE(r.success);
    const auto& o = std::get<ScreenSaverOptions>(r.options);
    REQUIRE(o.show.value_or(false));
    REQUIRE_FALSE(o.is_fade_in.value_or(true));
}

TEST_CASE("unknown verb silently means app (oracle-verified)", "[cmdline]") {
    // CommandLineParser picks the FIRST successful verb type; with all options
    // optional in AppOptions, "bogus ..." parses as plain "app".
    const auto r = parse("bogus");
    REQUIRE(r.success);
    REQUIRE(std::holds_alternative<AppOptions>(r.options));
}

TEST_CASE("missing value is a parse error", "[cmdline]") {
    REQUIRE_FALSE(parse("setwp --file").success);
}
