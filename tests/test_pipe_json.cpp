// PipeClient + JsonUtil/JsonStorage equivalence tests.
//
// PipeClient: a real Win32 named-pipe server thread accepts one connection
// and captures the raw bytes — verifying the C# semantics (no framing, raw
// payload, immediate-fail connect when nobody listens).
//
// JsonUtil/JsonStorage: file round-trips against the ported SettingsModel.
#include <catch2/catch_test_macros.hpp>

#include <lively/common/json_util.h>
#include <lively/common/pipe_client.h>
#include <lively/common/constants.h>
#include <lively/models/ipc_message.h>
#include <lively/models/settings_model.h>

#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>
#include <string>

using namespace lively;

TEST_CASE("PipeClient delivers raw message to a listening pipe", "[pipes]") {
    const std::wstring pipe_name = L"lively_test_pipe_client_send";
    const std::wstring full = L"\\\\.\\pipe\\" + pipe_name;

    HANDLE server = CreateNamedPipeW(
        full.c_str(),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    REQUIRE(server != INVALID_HANDLE_VALUE);

    std::string received;
    std::atomic<bool> done{false};
    std::thread reader([server, &received, &done] {
        if (ConnectNamedPipe(server, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buf[256];
            DWORD read = 0;
            while (ReadFile(server, buf, sizeof(buf), &read, nullptr) && read > 0) {
                received.append(buf, read);
                if (received.size() >= 5) break; // test payload is 5 bytes
            }
        }
        done = true;
    });

    const bool ok = common::PipeClient::send_message(pipe_name, "hello");
    REQUIRE(ok);
    reader.join();
    DisconnectNamedPipe(server);
    CloseHandle(server);

    // Raw bytes, no framing — exactly what C# StreamWriter.Write sends.
    CHECK(received == "hello");
}

TEST_CASE("PipeClient returns false immediately when no server listens", "[pipes]") {
    // C# Connect(0) → TimeoutException immediately; port returns false.
    const bool ok = common::PipeClient::send_message(
        L"lively_no_such_pipe_exists_9137", "x");
    CHECK_FALSE(ok);
}

TEST_CASE("JsonStorage stores and loads SettingsModel files", "[jsonutil]") {
    models::SettingsModel m;
    m.audio_volume_global = 61;
    m.language = "en-US";
    m.video_player = models::LivelyMediaPlayer::libmpv;

    const std::string path = (std::filesystem::temp_directory_path() /
                              "lively_cpp_test_settings.json").string();
    common::JsonStorage::StoreData(path, m);

    const auto loaded = common::JsonStorage::LoadData(path);
    CHECK(loaded.audio_volume_global == 61);
    CHECK(loaded.language == "en-US");
    CHECK(loaded.video_player == models::LivelyMediaPlayer::libmpv);
    CHECK(loaded.saved_url == "https://www.youtube.com/watch?v=aqz-KE-bpKQ");
    std::remove(path.c_str());
}

TEST_CASE("JsonStorage throws on corrupted settings file", "[jsonutil]") {
    // C# LoadData: corrupt json → ArgumentNullException (exception path).
    const std::string path = (std::filesystem::temp_directory_path() /
                              "lively_cpp_test_corrupt.json").string();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "{ not valid json !!";
    }
    CHECK_THROWS_AS(common::JsonStorage::LoadData(path), std::exception);
    std::remove(path.c_str());
}

TEST_CASE("JsonUtil Serialize uses compact form for IPC messages", "[jsonutil]") {
    models::LivelyVolumeCmd vol;
    vol.volume = 77;
    CHECK(common::JsonUtil::Serialize(vol) == "{\"Type\":9,\"Volume\":77}");
}

TEST_CASE("JsonUtil Write/ReadJObject round-trips indented JSON", "[jsonutil]") {
    const std::string path = (std::filesystem::temp_directory_path() /
                              "lively_cpp_test_jobj.json").string();
    common::JsonValue obj;
    obj["Name"] = "wallpaper";
    obj["Items"] = common::JsonValue::array({"a", "b"});

    common::JsonUtil::Write(path, obj);
    const auto back = common::JsonUtil::ReadJObject(path);
    CHECK(back["Name"] == "wallpaper");
    CHECK(back["Items"].size() == 2);
    std::remove(path.c_str());
}
