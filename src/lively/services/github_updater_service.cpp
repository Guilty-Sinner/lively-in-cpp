#include <lively/services/github_updater_service.h>

#include <lively/net/http_client.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>

// Local assembly version (C# reads Assembly.GetEntryAssembly().GetName().Version).
// The C++ port carries it as a build definition.
#ifndef LIVELY_APP_VERSION
#define LIVELY_APP_VERSION "0.0.0.0"
#endif

namespace lively::services {

namespace github_util {

std::string strip_tag_to_version(const std::string& tag_name) {
    // C# Regex.Replace(tag, "[A-Za-z ]", "") — keep digits and dots.
    std::string out;
    for (const char c : tag_name) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
            out += c;
    }
    // Trim trailing dots like Version parsing tolerates.
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out.empty() ? std::string("0.0.0.0") : out;
}

namespace {
std::vector<int> parse_version_parts(const std::string& s) {
    std::vector<int> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.')) {
        try { parts.push_back(std::stoi(part)); }
        catch (...) { parts.push_back(0); }
    }
    return parts;
}
} // namespace

int compare_versions(const std::string& a, const std::string& b) {
    std::vector<int> pa = parse_version_parts(a);
    std::vector<int> pb = parse_version_parts(b);
    const size_t n = std::max(pa.size(), pb.size());
    pa.resize(n, 0);
    pb.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

std::optional<GithubRelease::Asset> find_asset(const GithubRelease& release,
                                               const std::string& asset_name) {
    // C# FirstOrDefault(Contains(name, assetName, OrdinalIgnoreCase)).
    for (const auto& asset : release.assets) {
        const std::string& n = asset.name;
        if (n.size() < asset_name.size()) continue;
        const auto it = std::search(
            n.begin(), n.end(), asset_name.begin(), asset_name.end(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
        if (it != n.end()) return asset;
    }
    return std::nullopt;
}

} // namespace github_util

namespace {

// Real transport: GitHub REST API via the shared WinHTTP client.
class HttpGithubFetcher : public IGithubFetcher {
public:
    GithubRelease get_latest_release(const std::string& user_name,
                                     const std::string& repository_name) override {
        net::HttpRequest request;
        request.method = "GET";
        request.url = "https://api.github.com/repos/" + user_name + "/" + repository_name + "/releases";
        request.headers["User-Agent"] = "lively-cpp";
        request.headers["Accept"] = "application/vnd.github+json";
        const auto response = http_.send(request);
        if (!response.is_success())
            throw std::runtime_error("github releases fetch failed: " + std::to_string(response.status_code));

        const auto arr = nlohmann::json::parse(response.body, nullptr, false);
        if (arr.is_discarded() || !arr.is_array() || arr.empty())
            throw std::runtime_error("github releases: empty response");

        GithubRelease release;
        release.tag_name = arr[0].value("tag_name", "");
        for (const auto& a : arr[0].value("assets", nlohmann::json::array())) {
            release.assets.push_back({a.value("name", ""), a.value("browser_download_url", "")});
        }
        return release;
    }

private:
    net::HttpClient http_;
};

} // namespace

GithubUpdaterService::GithubUpdaterService(std::shared_ptr<IGithubFetcher> fetcher)
    : fetcher_(fetcher ? std::move(fetcher) : std::make_shared<HttpGithubFetcher>()) {}

GithubUpdaterService::~GithubUpdaterService() { stop(); }

AppUpdateStatus GithubUpdaterService::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::chrono::system_clock::time_point GithubUpdaterService::last_check_time() const {
    std::lock_guard lock(mutex_);
    return last_check_time_;
}

std::string GithubUpdaterService::last_check_version() const {
    std::lock_guard lock(mutex_);
    return last_check_version_;
}

std::optional<std::string> GithubUpdaterService::last_check_uri() const {
    std::lock_guard lock(mutex_);
    return last_check_uri_;
}

std::optional<std::string> GithubUpdaterService::last_check_file_name() const {
    std::lock_guard lock(mutex_);
    return last_check_file_name_;
}

void GithubUpdaterService::start() {
    {
        std::lock_guard lock(timer_mutex_);
        if (running_) return;
        running_ = true;
    }
    timer_thread_ = std::jthread([this] { timer_loop(); });
}

void GithubUpdaterService::stop() {
    {
        std::lock_guard lock(timer_mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    if (timer_thread_.joinable()) timer_thread_.join();
}

void GithubUpdaterService::timer_loop() {
    // C# Timer(5min) + elapsed check gating by 12h/30min delays.
    std::unique_lock lock(timer_mutex_);
    for (;;) {
        if (cv_.wait_for(lock, std::chrono::minutes(5), [this] { return !running_; }))
            return;
        lock.unlock();

        const auto last = last_check_time();
        const bool is_error = status() == AppUpdateStatus::error;
        const auto gate = std::chrono::milliseconds(is_error ? 30ull * 60 * 1000 : 12ull * 60 * 60 * 1000);
        if (std::chrono::system_clock::now() - last > gate)
            check_update(0);

        lock.lock();
    }
}

GithubUpdaterService::LatestRelease GithubUpdaterService::get_latest_release(bool is_beta) {
    const GithubRelease release =
        fetcher_->get_latest_release("rocksdanister", is_beta ? "lively-beta" : "lively");
    const std::string version = github_util::strip_tag_to_version(release.tag_name);

    auto asset = github_util::find_asset(release, std::string("lively_setup_") + arch_setup_string() + "_full");
    if (!asset && arch_setup_string() == std::string("x64")) {
        // Legacy x86-named x64 installer fallback.
        asset = github_util::find_asset(release, "lively_setup_x86_full");
    }
    LatestRelease out;
    out.version = version;
    if (asset) {
        out.file_name = asset->name;
        out.uri = asset->browser_download_url;
    }
    return out;
}

const char* GithubUpdaterService::arch_setup_string() {
    // C# RuntimeInformation.ProcessArchitecture (x64 builds only in practice).
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "x64";
#endif
}

AppUpdateStatus GithubUpdaterService::check_update(int fetch_delay_ms) {
    try {
        if (fetch_delay_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(fetch_delay_ms));

        const auto release_info = get_latest_release(false);
        const int ver_compare = github_util::compare_versions(release_info.version, LIVELY_APP_VERSION);

        std::lock_guard lock(mutex_);
        if (ver_compare > 0) status_ = AppUpdateStatus::available;
        else if (ver_compare < 0) status_ = AppUpdateStatus::invalid;
        else status_ = AppUpdateStatus::uptodate;
        last_check_uri_ = release_info.uri;
        last_check_version_ = release_info.version;
        last_check_file_name_ = release_info.file_name;
    } catch (...) {
        std::lock_guard lock(mutex_);
        status_ = AppUpdateStatus::error;
    }
    last_check_time_ = std::chrono::system_clock::now();

    AppUpdaterEventArgs args;
    {
        std::lock_guard lock(mutex_);
        args.status = status_;
        args.version = last_check_version_;
        args.check_time = last_check_time_;
        args.uri = last_check_uri_;
        args.file_name = last_check_file_name_;
    }
    update_checked.raise(args);
    return args.status;
}

} // namespace lively::services
