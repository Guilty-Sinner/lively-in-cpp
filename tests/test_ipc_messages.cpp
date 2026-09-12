#include <catch2/catch_test_macros.hpp>

#include <lively/models/ipc_message.h>

#include <nlohmann/json.hpp>

using namespace lively::models;

// Golden wire-format assertions.
// Expected strings derived from the C# Newtonsoft.Json behaviour of the
// Lively.Models sources: Type serializes as the enum ORDINAL (no
// StringEnumConverter on IpcMessage) and appears FIRST (JsonProperty(Order=-2));
// other properties follow in declaration order; enum properties serialize as
// ordinals; a null C# string becomes JSON null.
// tools/generate_csharp_goldens.md documents regenerating these from the real
// C# binaries (dotnet run) — until that runs, these encode the source-derived
// expectation and will fail loudly if the wire format drifts.

namespace {

std::string wire(const IpcMessage& msg) { return lively::models::serialize(msg); }

} // namespace

TEST_CASE("empty command messages serialize Type-only, Type first", "[ipc][golden]") {
    // Ordinals per Lively.Models/Message/MessageType.cs:
    // cmd_reload=4, cmd_close=5, cmd_suspend=7, cmd_resume=8.
    REQUIRE(wire(LivelyCloseCmd{}) == "{\"Type\":5}");
    REQUIRE(wire(LivelySuspendCmd{}) == "{\"Type\":7}");
    REQUIRE(wire(LivelyResumeCmd{}) == "{\"Type\":8}");
    REQUIRE(wire(LivelyReloadCmd{}) == "{\"Type\":4}");
}

TEST_CASE("LivelyVolumeCmd", "[ipc][golden]") {
    LivelyVolumeCmd m;
    REQUIRE(wire(m) == "{\"Type\":9,\"Volume\":0}");
    m.volume = 42;
    REQUIRE(wire(m) == "{\"Type\":9,\"Volume\":42}");
}

TEST_CASE("LivelyMessageHwnd", "[ipc][golden]") {
    LivelyMessageHwnd m;
    m.hwnd = 65535;
    REQUIRE(wire(m) == "{\"Type\":0,\"Hwnd\":65535}");
}

TEST_CASE("LivelyMessageWallpaperLoaded", "[ipc][golden]") {
    LivelyMessageWallpaperLoaded m;
    m.success = true;
    REQUIRE(wire(m) == "{\"Type\":2,\"Success\":true}");
}

TEST_CASE("LivelyMessageConsole category is ordinal", "[ipc][golden]") {
    LivelyMessageConsole m;
    m.message = "hello";
    m.category = ConsoleMessageType::error;
    REQUIRE(wire(m) == "{\"Type\":1,\"Message\":\"hello\",\"Category\":1}");
}

TEST_CASE("livelyProperty updates keep declaration order", "[ipc][golden]") {
    LivelySlider s;
    s.name = "Speed";
    s.value = 1.5;
    s.step = 0.5;
    REQUIRE(wire(s) == "{\"Type\":12,\"Name\":\"Speed\",\"Value\":1.5,\"Step\":0.5}");

    LivelyTextBox t;
    t.name = "Note";
    t.value = "abc";
    REQUIRE(wire(t) == "{\"Type\":13,\"Name\":\"Note\",\"Value\":\"abc\"}");

    LivelyCheckbox c;
    c.name = "Enabled";
    c.value = true;
    REQUIRE(wire(c) == "{\"Type\":18,\"Name\":\"Enabled\",\"Value\":true}");

    LivelyDropdown d;
    d.name = "Quality";
    d.value = 2;
    REQUIRE(wire(d) == "{\"Type\":14,\"Name\":\"Quality\",\"Value\":2}");

    LivelyButton b;
    b.name = "Apply";
    b.is_default = true;
    REQUIRE(wire(b) == "{\"Type\":16,\"Name\":\"Apply\",\"IsDefault\":true}");

    LivelyColorPicker p;
    p.name = "Tint";
    p.value = "#FFAA00";
    REQUIRE(wire(p) == "{\"Type\":17,\"Name\":\"Tint\",\"Value\":\"#FFAA00\"}");
}

TEST_CASE("round-trip parse of incoming volume command", "[ipc]") {
    // Incoming path: wallpaper -> lively (players parse this JSON in C#).
    const auto j = nlohmann::ordered_json::parse("{\"Type\":9,\"Volume\":30}");
    REQUIRE(j["Type"].get<int>() == static_cast<int>(MessageType::cmd_volume));
    REQUIRE(j["Volume"].get<int>() == 30);
}
