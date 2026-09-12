// Characterization tests for the LivelyProperty pipeline.
// The golden file tests/goldens/props_csharp.txt was produced by running the
// REAL C# LivelyPropertyUtil + LivelyControlModelConverter (via
// tools/csharp_probe "props" mode) over the repository's own
// Assets/Plugins/Mpv/LivelyProperties.json + .loc.json. The C++ port must
// reproduce it line-for-line.
#include <catch2/catch_test_macros.hpp>

#include <lively/models/lively_controls.h>

#include <fstream>
#include <string>
#include <vector>

#ifndef LIVELY_GOLDENS_DIR
#define LIVELY_GOLDENS_DIR "tests/goldens"
#endif

#ifndef LIVELY_REPO_ROOT
#define LIVELY_REPO_ROOT ".."
#endif

using namespace lively::models;

namespace {

std::vector<std::pair<std::string, std::string>> load_golden_lines(const char* name) {
    std::vector<std::pair<std::string, std::string>> out;
    std::ifstream in(std::string(LIVELY_GOLDENS_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("#", 0) == 0) {
            out.emplace_back(line, ""); // marker lines carry no tab; keep for replay
            continue;
        }
        const auto tab = line.find('\t');
        REQUIRE(tab != std::string::npos);
        out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    return out;
}

std::string repo_file(const char* rel) {
    return std::string(LIVELY_REPO_ROOT) + "/lively in C#/src/Lively/Lively/Assets/Plugins/Mpv/" + rel;
}

} // namespace

TEST_CASE("C++ LivelyProperty parse equals C# over the real Mpv properties", "[props][oracle]") {
    const std::string props_path = repo_file("LivelyProperties.json");
    std::ifstream probe(props_path);
    REQUIRE(probe.good()); // repo fixture present

    ControlMap controls = LivelyPropertyUtil::GetControlsFromFile(props_path);
    REQUIRE(controls.size() == 8);

    // Replay the probe protocol: golden lines before the "# localized <lang>"
    // marker compare against the raw parse; after applying LocalizeControls
    // (same lang), the remaining lines must match the localized controls.
    const auto goldens = load_golden_lines("props_csharp.txt");
    bool localized = false;
    std::size_t checked = 0;
    for (const auto& [key, expected] : goldens) {
        if (key.rfind("# localized ", 0) == 0) {
            const std::string lang = key.substr(std::string("# localized ").size());
            std::ifstream loc_in(repo_file("LivelyProperties.loc.json"), std::ios::binary);
            REQUIRE(loc_in.good());
            const auto loc = nlohmann::ordered_json::parse(loc_in, nullptr, true, true);
            LivelyPropertyUtil::LocalizeControls(loc, controls, lang);
            localized = true;
            continue;
        }
        const auto it = controls.find(key);
        REQUIRE(it != controls.end());
        INFO("stage: " << (localized ? "localized" : "raw") << " control: " << key
                       << "\n  csharp: " << expected
                       << "\n  c++:    " << describe_control(*it->second));
        REQUIRE(describe_control(*it->second) == expected);
        ++checked;
    }
    REQUIRE(checked == 16); // 8 raw + 8 localized
    REQUIRE(localized);
}

TEST_CASE("C# JsonIgnore wins over JsonProperty for SliderModel.Tick", "[props][oracle]") {
    // LivelyProperties.json sets "tick": 200; C# oracle shows tick=0.
    const auto controls = LivelyPropertyUtil::GetControlsFromFile(
        repo_file("LivelyProperties.json"));
    const auto* saturation = dynamic_cast<const SliderModel*>(controls.at("saturation").get());
    REQUIRE(saturation != nullptr);
    REQUIRE(saturation->tick == 0); // NOT 200 — matches C# behavior
    REQUIRE(saturation->min == -100);
    REQUIRE(saturation->max == 100);
}

TEST_CASE("slider step defaults to 1 when missing (C# comment rationale)", "[props]") {
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(
        R"({"saturation": {"type":"slider","min":0,"max":10,"value":5}})");
    const auto controls = LivelyPropertyUtil::GetControls(j);
    const auto* s = dynamic_cast<const SliderModel*>(controls.at("saturation").get());
    REQUIRE(s->step == 1.0);
}

TEST_CASE("control without name takes the property key (converter rule)", "[props]") {
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(
        R"({"myKey": {"type":"textbox","value":"v"}})");
    const auto controls = LivelyPropertyUtil::GetControls(j);
    REQUIRE(controls.at("myKey")->name == "myKey");
}

TEST_CASE("explicit name overrides key", "[props]") {
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(
        R"({"myKey": {"type":"textbox","name":"explicit","value":"v"}})");
    const auto controls = LivelyPropertyUtil::GetControls(j);
    REQUIRE(controls.at("myKey")->name == "explicit");
}

TEST_CASE("unsupported control type throws like NotSupportedException", "[props]") {
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(
        R"({"bad": {"type":"mystery"}})");
    REQUIRE_THROWS_AS(LivelyPropertyUtil::GetControls(j), std::runtime_error);
}

TEST_CASE("localization applies text/help/items with base-language fallback",
          "[props][oracle]") {
    auto controls = LivelyPropertyUtil::GetControlsFromFile(
        repo_file("LivelyProperties.json"));

    std::ifstream loc_in(repo_file("LivelyProperties.loc.json"), std::ios::binary);
    REQUIRE(loc_in.good());
    const auto loc = nlohmann::ordered_json::parse(loc_in, nullptr, true, true);

    SECTION("exact match: zh-CN") {
        LivelyPropertyUtil::LocalizeControls(loc, controls, "zh-CN");
        REQUIRE(controls.at("saturation")->text == "\xE9\xA3\xBD\xE5\x92\x8C\xE5\xBA\xA6"); // 飽和度
        const auto* scaler = dynamic_cast<const ScalerDropdownModel*>(controls.at("scaler").get());
        REQUIRE(scaler->items.size() == 4);
        REQUIRE(scaler->items[0] == "\xE7\x84\xA1"); // 無
    }

    SECTION("base fallback: zh-XX -> zh base is absent -> unchanged") {
        // zh base language does not exist in the file (only zh-CN / zh-Hant),
        // so a made-up zh-XX leaves controls untouched — same as C#.
        LivelyPropertyUtil::LocalizeControls(loc, controls, "zh-XX");
        REQUIRE(controls.at("saturation")->text == "Saturation");
    }

    SECTION("base fallback: es-419 -> es") {
        LivelyPropertyUtil::LocalizeControls(loc, controls, "es-419");
        REQUIRE(controls.at("mute")->text != "Mute");
    }
}

TEST_CASE("LoadProperty value extraction matches C# switch", "[props]") {
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(
        R"({"a": {"type":"slider","value":2.5},
            "b": {"type":"checkbox","value":true},
            "c": {"type":"dropdown","value":1},
            "d": {"type":"textbox","value":"t"},
            "e": {"type":"button","value":"skip"}})");
    const auto controls = LivelyPropertyUtil::GetControls(j);

    nlohmann::ordered_json out;
    REQUIRE(controls.at("a")->try_get_value(out)); REQUIRE(out.dump() == "2.5");
    REQUIRE(controls.at("b")->try_get_value(out)); REQUIRE(out.dump() == "true");
    REQUIRE(controls.at("c")->try_get_value(out)); REQUIRE(out.dump() == "1");
    REQUIRE(controls.at("d")->try_get_value(out)); REQUIRE(out.dump() == "\"t\"");
    // Buttons are user-interaction-only: no value emitted.
    REQUIRE_FALSE(controls.at("e")->try_get_value(out));
}
