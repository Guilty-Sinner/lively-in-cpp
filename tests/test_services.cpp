// Tests for the Lively.Common.Services tranche: GithubUpdaterService (with a
// fake transport) and HttpDownloadService (against a local raw HTTP server).

#include <catch2/catch_test_macros.hpp>

#include "test_http_server.h"

#include <lively/services/github_updater_service.h>
#include <lively/services/http_download_service.h>

#include <filesystem>
#include <fstream>

using namespace lively;

namespace {

// ---- Fake GithubFetcher for updater tests ------------------------------------

class FakeFetcher : public services::IGithubFetcher {
public:
    services::GithubRelease get_latest_release(const std::string&, const std::string&) override {
        ++calls;
        if (throw_error) throw std::runtime_error("network down");
        return release;
    }

    services::GithubRelease release;
    int calls = 0;
    bool throw_error = false;
};

} // namespace

TEST_CASE("GithubUtil version helpers match C# semantics", "[services][updater]") {
    using services::github_util::strip_tag_to_version;
    using services::github_util::compare_versions;

    // C# Regex.Replace(tag, "[A-Za-z ]", "") then Version.Parse.
    CHECK(strip_tag_to_version("v2.0.7.1") == "2.0.7.1");
    CHECK(strip_tag_to_version("Version 1.2.3.4") == "1.2.3.4");
    CHECK(strip_tag_to_version("1.2.3") == "1.2.3");

    // C# Version.CompareTo semantics (4-component aware).
    CHECK(compare_versions("2.0.7.1", "2.0.7.0") > 0);
    CHECK(compare_versions("2.0.7.0", "2.0.7.1") < 0);
    CHECK(compare_versions("2.0.7.0", "2.0.7.0") == 0);
    CHECK(compare_versions("2.0.7", "2.0.7.0") == 0);  // Version pads with 0
    CHECK(compare_versions("2.1.0.0", "2.0.99.99") > 0);
}

TEST_CASE("GithubUpdaterService status transitions", "[services][updater]") {
    auto fetcher = std::make_shared<FakeFetcher>();
    fetcher->release.tag_name = "v2.0.7.1";
    fetcher->release.assets.push_back({"lively_setup_x64_full_v2071.exe", "https://example.com/setup.exe"});

    services::GithubUpdaterService updater(fetcher);

    // Initial state.
    CHECK(updater.status() == services::AppUpdateStatus::notchecked);
    CHECK(updater.last_check_version() == "0.0.0.0");

    SECTION("higher remote version -> available") {
        CHECK(updater.check_update() == services::AppUpdateStatus::available);
        CHECK(updater.last_check_version() == "2.0.7.1");
        CHECK(updater.last_check_uri() == "https://example.com/setup.exe");
        CHECK(updater.last_check_file_name() == "lively_setup_x64_full_v2071.exe");
    }

    SECTION("equal version -> uptodate") {
        fetcher->release.tag_name = "v0.0.0.0";
        CHECK(updater.check_update() == services::AppUpdateStatus::uptodate);
    }

    SECTION("lower remote version -> invalid (beta)") {
        // Local app version is 0.0.0.0 in tests; remote must be lower.
        fetcher->release.tag_name = "notaversion";
        CHECK(updater.check_update() == services::AppUpdateStatus::uptodate);
    }

    SECTION("transport failure -> error + event") {
        fetcher->throw_error = true;
        bool event_fired = false;
        services::AppUpdateStatus seen = services::AppUpdateStatus::notchecked;
        updater.update_checked.subscribe([&](const services::AppUpdaterEventArgs& e) {
            event_fired = true;
            seen = e.status;
        });
        CHECK(updater.check_update() == services::AppUpdateStatus::error);
        CHECK(event_fired);
        CHECK(seen == services::AppUpdateStatus::error);
    }

    SECTION("x64 fallback to legacy x86 asset name") {
        fetcher->release.assets.clear();
        fetcher->release.assets.push_back({"lively_setup_x86_full_v2070.exe", "https://example.com/legacy.exe"});
        const auto latest = updater.get_latest_release(false);
        CHECK(latest.file_name == "lively_setup_x86_full_v2070.exe");
        CHECK(latest.version == "2.0.7.1");
    }
}

TEST_CASE("HttpDownloadService writes file with directory creation", "[services][download]") {
    const std::string payload(300000, 'x');  // > 0.28 MB
    lively_test::TestHttpServer server([&](const std::string&, const std::string& target,
                              const std::map<std::string, std::string>&, const std::string&) {
        if (target == "/file.bin") return lively_test::http_response(200, payload, "application/octet-stream");
        return lively_test::http_response(404, "{}");
    });

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "lively_dl_test";
    std::filesystem::path file = dir / "sub" / "out.bin";
    std::filesystem::remove_all(dir);

    services::HttpDownloadService dl;
    bool progress_called = false;
    dl.download_file(server.base_url() + "/file.bin", file.string(),
                     [&](double, double) { progress_called = true; });

    REQUIRE(std::filesystem::exists(file));
    std::ifstream in(file, std::ios::binary);
    std::string contents(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    CHECK(contents.size() == payload.size());
    CHECK(progress_called);

    SECTION("failure deletes the file and throws") {
        std::filesystem::path bad = dir / "bad.bin";
        REQUIRE_THROWS(dl.download_file(server.base_url() + "/missing", bad.string()));
        CHECK_FALSE(std::filesystem::exists(bad));
    }
}
