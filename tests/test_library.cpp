// Port of the Lively.Common "library" tranche — the headless core.
//
// Two layers, matching the pattern used by the other oracle tests:
//
//   1. `canonical_report()` builds a fixture tree and emits one `key<TAB>value`
//      line per behaviour, in a fixed order, with every machine-specific path
//      replaced by <root>/<lap>. tests/goldens/library_csharp.txt holds the same
//      transcript produced by the REAL C# code (tools/csharp_probe library), and
//      the report is compared line for line — so a rule that drifts anywhere in
//      FileTypes/FileUtil/LinkUtil/LivelyInfo/Languages/WallpaperLibraryFactory
//      shows up as a named failing line.
//   2. Focused checks for the parts that need real archives or were not part of
//      the transcript (zip package detection over committed fixtures, the
//      System.IO.Path primitives, JSON round-trips).

#include <catch2/catch_test_macros.hpp>

#include <lively/common/constants.h>
#include <lively/common/factories/wallpaper_library_factory.h>
#include <lively/common/file_types.h>
#include <lively/common/file_util.h>
#include <lively/common/json_util.h>
#include <lively/common/languages.h>
#include <lively/common/link_util.h>
#include <lively/common/lively_info_util.h>
#include <lively/common/path_util.h>
#include <lively/common/wallpaper_extensions.h>
#include <lively/common/window_class_exclusions.h>
#include <lively/models/library_model.h>
#include <lively/models/lively_info.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace lively;
using namespace lively::common;
using models::LivelyInfoModel;
using models::WallpaperType;

namespace {

namespace fs = std::filesystem;

#ifdef LIVELY_GOLDENS_DIR
const std::string kGoldensDir = LIVELY_GOLDENS_DIR;
#else
const std::string kGoldensDir = "tests/goldens";
#endif

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// ---------------------------------------------------------------- escaping

std::string hex2(unsigned value) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.push_back(digits[(value >> 4) & 0xF]);
    out.push_back(digits[value & 0xF]);
    return out;
}

// Transcript key escaping: keeps '|' usable as an in-value separator.
std::string escape_key(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '|': out += "\\p"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) out += "\\x" + hex2(static_cast<unsigned char>(c));
            else out.push_back(c);
        }
    }
    return out.empty() ? std::string("<empty>") : out;
}

// JSON payload escaping (one line per check, \r\n stays visible).
std::string escaped_payload(const std::string& payload) {
    std::string out;
    for (const char c : payload) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '"': out += "\\\""; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) out += "\\x" + hex2(static_cast<unsigned char>(c));
            else out.push_back(c);
        }
    }
    return out;
}

std::string text(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::string v(const std::optional<std::string>& s) {
    return s.has_value() ? "[" + text(*s) + "]" : std::string("<null>");
}

std::string scratch_dir() {
    return (fs::temp_directory_path() / "lively_library_probe").string();
}

std::string replace_all(std::string haystack, const std::string& from, const std::string& to) {
    if (from.empty()) return haystack;
    std::size_t at = 0;
    while ((at = haystack.find(from, at)) != std::string::npos) {
        haystack.replace(at, from.size(), to);
        at += to.size();
    }
    return haystack;
}

bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

// The probe's PathRoots(): strip the machine-specific prefixes, then normalize
// separators so the transcript is identical on any machine.
std::string path_roots(std::string p) {
    const std::string root = scratch_dir();
    if (starts_with_ci(p, root)) p = "<root>" + p.substr(root.size());
    const std::string lap = common::UserLocalAppDataDir();
    if (!lap.empty() && starts_with_ci(p, lap)) p = "<lap>" + p.substr(lap.size());
    return replace_all(p, "\\", "/");
}

std::string rel(const std::optional<std::string>& s) {
    return s.has_value() ? "[" + text(path_roots(*s)) + "]" : std::string("<null>");
}

std::string rel_path(const std::optional<std::string>& s) { return rel(s); }

// -------------------------------------------------------------- transcript

std::string g_report;

void line(const std::string& key, const std::string& value) {
    g_report += key;
    g_report += '\t';
    g_report += value;
    g_report += '\n';
}

std::string bool_str(bool value) { return value ? "true" : "false"; }

std::string joined(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += separator;
        out += parts[i];
    }
    return out;
}

std::optional<std::string> opt_or_null(const char* value) {
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

std::string describe_info(const LivelyInfoModel& m) {
    std::vector<std::string> tags;
    if (m.tags.has_value()) {
        for (const auto& t : *m.tags) tags.push_back(text(t));
    }
    return joined(
        {
            "title=" + v(m.title),
            "thumb=" + v(m.thumbnail.has_value() ? std::optional<std::string>(path_roots(*m.thumbnail)) : std::nullopt),
            "preview=" + v(m.preview.has_value() ? std::optional<std::string>(path_roots(*m.preview)) : std::nullopt),
            "desc=" + v(m.desc),
            "author=" + v(m.author),
            "license=" + v(m.license),
            "contact=" + v(m.contact.has_value() ? std::optional<std::string>(path_roots(*m.contact)) : std::nullopt),
            "type=" + std::to_string(static_cast<int>(m.type)),
            "file=" + v(m.file_name.has_value() ? std::optional<std::string>(path_roots(*m.file_name)) : std::nullopt),
            "args=" + v(m.arguments),
            "abs=" + bool_str(m.is_absolute_path),
            "id=" + v(m.id),
            "tags=" + (m.tags.has_value() ? joined(tags, "|") : std::string("<null>")),
            "version=" + std::to_string(m.version),
        },
        ";");
}

std::string describe_library(const models::LibraryModel& lib) {
    return joined(
        {
            "title=" + v(lib.title()),
            "desc=" + v(lib.desc()),
            "author=" + v(lib.author()),
            "subscribed=" + bool_str(lib.is_subscribed),
            "file=" + rel(lib.file_path),
            "folder=" + rel(lib.lively_info_folder_path),
            "locpath=" + rel(lib.lively_info_localization_path),
            "proploc=" + rel(lib.lively_property_localization_path),
            "prop=" + rel(lib.lively_property_path),
            "preview=" + rel(lib.preview_clip_path),
            "thumb=" + rel(lib.thumbnail_path),
            "image=" + rel(lib.image_path),
        },
        ";");
}

// Maps the port's named failures back onto the BCL exception names the C#
// factory produces, so the transcript stays comparable.
std::string throws_name(const std::function<void()>& action) {
    try {
        action();
        return "<none>";
    } catch (const common::WallpaperMetadataNotFoundException&) {
        return "FileNotFoundException";
    } catch (const common::WallpaperMetadataCorruptedException&) {
        return "JsonReaderException";
    } catch (const common::WallpaperAlreadyPackagedException&) {
        return "InvalidOperationException";
    } catch (const std::exception& e) {
        return std::string("other:") + e.what();
    }
}

// ------------------------------------------------------------- the report

const std::string kCanonicalAppVersion = "0.0.0.0";

std::string store_lively_info(const std::string& path, const LivelyInfoModel& info) {
    common::JsonStorage::StoreLivelyInfoData(path, info);
    return info.to_json_string();
}

void emit_models() {
    line("livelyinfo.appversion", LivelyInfoModel{}.app_version);

    LivelyInfoModel default_model;
    default_model.app_version = kCanonicalAppVersion;
    const std::string root = scratch_dir();
    line("livelyinfo.default.serialize",
         escaped_payload(store_lively_info(path::combine(root, "model.json"), default_model)));

    LivelyInfoModel full;
    full.app_version = kCanonicalAppVersion;
    full.title = "A \"quoted\" title";
    full.thumbnail = "thumb.png";
    full.preview = std::nullopt;
    full.desc = "line1\nline2\ttabbed";
    full.author = "Ünïcode ✓";
    full.license = "GPL-3.0";
    full.contact = "https://example.com/u/1";
    full.type = WallpaperType::video;
    full.file_name = "video.mp4";
    full.arguments = "--loop true";
    full.is_absolute_path = true;
    full.id = "abc123";
    full.tags = std::vector<std::string>{"tag1", "タグ2", "tag/3"};
    full.version = 4;

    const std::string full_json = store_lively_info(path::combine(root, "model.json"), full);
    line("livelyinfo.full.serialize", escaped_payload(full_json));

    write_file(path::combine(root, "reparse.json"), full_json);
    line("livelyinfo.reparsed",
         describe_info(common::JsonStorage::LoadLivelyInfoData(path::combine(root, "reparse.json"))));

    write_file(path::combine(root, "sparse.json"), "{\"Title\":\"only-title\"}");
    line("livelyinfo.sparse",
         describe_info(common::JsonStorage::LoadLivelyInfoData(path::combine(root, "sparse.json"))));

    models::LibraryModel blank;
    blank.set_title("  ");
    models::LibraryModel null_title;
    null_title.set_title(std::nullopt);
    models::LibraryModel real_title;
    real_title.set_title("Real");
    models::LibraryModel defaults;
    line("librarymodel.blank-title", blank.title());
    line("librarymodel.null-title", null_title.title());
    line("librarymodel.set-title", real_title.title());
    line("librarymodel.defaults",
         joined({defaults.is_ready_to_set ? "ready" : "not-ready", defaults.downloading_progress_text,
                 defaults.is_subscribed ? "sub" : "unsub"},
                ";"));
}

void emit_file_types() {
    const std::vector<std::string> extensions = {
        ".wmv", ".avi", ".flv", ".m4v", ".mkv", ".mov", ".mp4", ".mp4v", ".mpeg4", ".mpg",
        ".webm", ".ogm", ".ogv", ".ogx",
        ".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp", ".jfif",
        ".gif", ".html", ".exe", ".zip", ".txt", ".heic", ".MP4", ".PNG", ".Exe", ".GIF",
        "", ".", ".htm",
    };
    for (const auto& ext : extensions) {
        line("filetypes.gettype." + (ext.empty() ? std::string("<none>") : ext),
             std::to_string(common::get_file_type("wallpaper" + ext)));
    }

    for (const char* name : {"a.zip", "a.ZIP", "a.Zip", "a.7z", "zip", "a.zipx"}) {
        line(std::string("filetypes.ispackext.") + name,
             bool_str(common::is_wallpaper_package_extension(name)));
    }

    // Written with python's zipfile into tests/goldens/fixtures, mirroring the
    // archives SharpZipLib writes in the probe (same entry names).
    struct PkgFixture {
        const char* file;
        const char* label;
    };
    const PkgFixture fixtures[] = {
        {"package_ok.zip", "ok"},         {"package_nested.zip", "nested"},
        {"package_case.zip", "case"},     {"package_no_meta.zip", "nometa"},
        {"not_a_zip.zip", "garbage"},
    };
    for (const auto& fixture : fixtures) {
        const std::string path = kGoldensDir + "/fixtures/" + fixture.file;
        line(std::string("filetypes.ispkg.") + fixture.label + ".zip",
             bool_str(common::is_wallpaper_package(path)));
    }
    line("filetypes.ispkg.missing.zip",
         bool_str(common::is_wallpaper_package(scratch_dir() + "\\nope.zip")));

    const auto& formats = common::supported_formats();
    for (std::size_t i = 0; i < formats.size(); ++i) {
        std::string value = std::to_string(static_cast<int>(formats[i].type)) + ":";
        for (std::size_t k = 0; k < formats[i].extensions.size(); ++k) {
            if (k > 0) value += ",";
            value += formats[i].extensions[k];
        }
        line("filetypes.supported." + std::to_string(i), value);
    }
}

void emit_link_util() {
    const std::vector<std::string> urls = {
        "https://example.com/",
        "https://example.com",
        "https://example.com//",
        "https://www.example.com/",
        "https://www.example.com/a/b.html",
        "http://example.com/path/to/file.gif?v=2",
        "https://example.com/dir/",
        "https://example.com/a?x=1",
        "https://example.com/#frag",
        "https://example.com/a//b",
        "HTTPS://EXAMPLE.COM/Path/File.HTML",
        "not a url",
        "",
        "/local/path/file.html",
        "local/path/file.html",
        "/file.html",
        "C:\\local\\path\\file.html",
        "file:///C:/local/path/file.html",
    };
    for (const auto& url : urls) {
        line("linkutil.lastsegment." + escape_key(url),
             "[" + common::get_last_segment_url(url) + "]");
    }

    for (const char* p : {"C:\\wallpapers\\one\\file.html", "C:\\wallpapers\\two\\file.html",
                          "C:\\wallpapers\\two\\nested\\file.html", "relative\\dir\\file.html"}) {
        line(std::string("linkutil.stablehost.") + escape_key(p), common::get_stable_host_name(p));
    }

    for (const char* u : {"example.com/page", "https://example.com/page", "http://example.com",
                          "https://example.com:8080/x", "/abs/path", "  "}) {
        const auto sanitized = common::try_sanitize_url(u);
        line("linkutil.sanitize." + escape_key(u), sanitized.has_value() ? *sanitized : "<invalid>");
    }
}

void emit_file_util() {
    for (const char* name : {"plain", "a/b", "a\\b", "a:b", "a*b?c", "a<b>c|d\"e", "dot.name.txt",
                             "con", "имя файла", "trailing ", " leading"}) {
        line("fileutil.safefilename." + escape_key(name),
             "[" + common::get_safe_filename(name) + "]");
    }

    const std::string next_dir = path::combine(scratch_dir(), "nextavail");
    fs::create_directories(next_dir);
    for (const char* f : {"a.txt", "a (1).txt", "a (2).txt", "b", "b (1)", "c.txt"}) {
        write_file(path::combine(next_dir, f), "x");
    }
    for (const char* q : {"a.txt", "b", "c.txt", "d.txt"}) {
        line(std::string("fileutil.nextavailable.") + q,
             path::get_file_name(common::next_available_filename(path::combine(next_dir, q))));
    }

    const std::string sample = path::combine(scratch_dir(), "checksum.txt");
    write_file(sample, "hello world");
    line("fileutil.sha256.helloworld", common::get_checksum_sha256(sample));

    for (const long long value : {0LL, 1LL, 999LL, 1000LL, 1023LL, 1024LL, 1536LL, 999999LL,
                                  1048576LL, 1234567890LL, 1099511627776LL, -1LL, -1536LL}) {
        line("fileutil.sizesuffix." + std::to_string(value),
             "[" + common::size_suffix(value) + "]");
    }
    for (const int places : {0, 1, 2, 3}) {
        line("fileutil.sizesuffix.1536." + std::to_string(places),
             "[" + common::size_suffix(1536, places) + "]");
    }
    line("fileutil.sizesuffix.2560.0", "[" + common::size_suffix(2560, 0) + "]");
    line("fileutil.sizesuffix.3584.0", "[" + common::size_suffix(3584, 0) + "]");

    for (const long long size : {0LL, 5LL, 6LL}) {
        line("fileutil.isgreater.5." + std::to_string(size),
             bool_str(common::is_file_greater(sample, size)));
    }

    const std::string glob_dir = path::combine(scratch_dir(), "glob");
    fs::create_directories(path::combine(glob_dir, "sub"));
    for (const char* f : {"b.gif", "a.png", "c.html", "sub/d.png", "note.txt"}) {
        write_file(path::combine(glob_dir, f), "x");
    }
    auto names = [](const std::vector<std::string>& paths) {
        std::vector<std::string> out;
        for (const auto& p : paths) out.push_back(path::get_file_name(p));
        return joined(out, ",");
    };
    line("fileutil.getfiles.topmost", names(common::get_files(glob_dir, "*.png|*.gif", false)));
    line("fileutil.getfiles.all",
         names(common::get_files(glob_dir, "*.png|*.gif|*.html", true)));
    line("fileutil.getfiles.none", names(common::get_files(glob_dir, "*.webp", false)));
    line("fileutil.dirsize", std::to_string(common::get_directory_size(glob_dir)));

    nlohmann::ordered_json jobj;
    jobj["b"] = 1;
    jobj["a"] = nlohmann::ordered_json::array({1, 2});
    jobj["nested"] = nlohmann::ordered_json{{"x", nullptr}};
    jobj["text"] = "line1\nline2";
    const std::string json_util_path = path::combine(scratch_dir(), "jsonutil.json");
    common::JsonUtil::Write(json_util_path, jobj);
    line("jsonutil.write", escaped_payload(read_file(json_util_path)));
}

void emit_wallpaper_extensions() {
    const WallpaperType types[] = {
        WallpaperType::app,        WallpaperType::web,        WallpaperType::webaudio,
        WallpaperType::url,        WallpaperType::bizhawk,    WallpaperType::unity,
        WallpaperType::godot,      WallpaperType::video,      WallpaperType::gif,
        WallpaperType::unityaudio, WallpaperType::videostream, WallpaperType::picture,
    };
    const char* names[] = {"app", "web", "webaudio", "url", "bizhawk", "unity",
                           "godot", "video", "gif", "unityaudio", "videostream", "picture"};
    for (std::size_t i = 0; i < 12; ++i) {
        const WallpaperType t = types[i];
        const std::string prefix = std::string("wallpaperext.") + names[i];
        line(prefix + ".online", bool_str(common::is_online_wallpaper(t)));
        line(prefix + ".web", bool_str(common::is_web_wallpaper(t)));
        line(prefix + ".video", bool_str(common::is_video_wallpaper(t)));
        line(prefix + ".app", bool_str(common::is_application_wallpaper(t)));
        line(prefix + ".media", bool_str(common::is_media_wallpaper(t)));
        line(prefix + ".localweb", bool_str(common::is_local_web_wallpaper(t)));
        line(prefix + ".directoryproject", bool_str(common::is_directory_project(t)));
        line(prefix + ".deviceinput", bool_str(common::is_device_input_allowed(t)));
    }
}

void emit_languages() {
    const auto& languages = common::supported_languages();
    line("languages.count", std::to_string(languages.size()));
    for (std::size_t i = 0; i < languages.size(); ++i) {
        line("languages." + std::to_string(i),
             languages[i].display_name + "|" + languages[i].code);
    }
}

void emit_window_classes() {
    auto classes = common::desktop_classes();
    std::sort(classes.begin(), classes.end());
    line("windowclasses.count", std::to_string(classes.size()));
    for (std::size_t i = 0; i < classes.size(); ++i) {
        line("windowclasses." + std::to_string(i), classes[i]);
    }
    line("windowclasses.case-insensitive", bool_str(common::is_desktop_class("workerw")));
}

void emit_localization() {
    const std::string loc_path = path::combine(scratch_dir(), "LivelyInfo.loc.json");
    write_file(loc_path,
               "{\n"
               "  \"Languages\": {\n"
               "    \"en-US\": { \"Title\": \"English title\", \"Desc\": \"English desc\" },\n"
               "    \"zh-CN\": { \"Title\": \"中文标题\" },\n"
               "    \"zh\":    { \"Title\": \"base zh\" }\n"
               "  }\n"
               "}");

    for (const char* lang : {"fr-FR", "en-US", "zh-CN", "ZH-CN", "zh", "zh-Hans", "es-ES"}) {
        const auto info = common::get_localized_info(loc_path, lang);
        line(std::string("livelyinfoutil.localized.") + lang,
             info.has_value() ? v(info->title) + "|" + v(info->desc) : "<null>");
    }
    line("livelyinfoutil.missing",
         common::get_localized_info(path::combine(scratch_dir(), "nope.json"), "en-US").has_value()
             ? "value"
             : "<null>");
    const std::string corrupt = path::combine(scratch_dir(), "corrupt.loc.json");
    write_file(corrupt, "{ not json");
    line("livelyinfoutil.corrupt",
         common::get_localized_info(corrupt, "en-US").has_value() ? "value" : "<null>");
}

void emit_library_factory() {
    common::WallpaperLibraryFactory factory;
    const std::string root = scratch_dir();

    struct Case {
        const char* name;
        WallpaperType type;
        bool absolute;
        const char* file;
        const char* preview;
        const char* thumb;
    };
    const Case cases[] = {
        {"absapp", WallpaperType::app, true, "main.exe", "prev.png", "thumb.png"},
        {"relweb", WallpaperType::web, false, "index.html", "prev.png", nullptr},
        {"online", WallpaperType::url, false, "https://example.com/wall", nullptr, nullptr},
        {"video", WallpaperType::video, false, "clip.mp4", nullptr, "thumb.png"},
        {"mediaonly", WallpaperType::picture, false, "pic.png", "prev.png", nullptr},
        {"missing-all", WallpaperType::web, false, "index.html", "nope.png", "nope.png"},
    };

    for (const auto& c : cases) {
        const std::string dir = path::combine(path::combine(root, "lib"), c.name);
        fs::create_directories(dir);
        for (const char* f : {c.file, c.preview, c.thumb}) {
            if (f == nullptr || std::string(f).rfind("http", 0) == 0 || std::string(f).rfind("nope", 0) == 0) {
                continue;
            }
            const std::string name = path::get_file_name(f);
            write_file(path::combine(dir, name), "x");
            if (name == "main.exe") {
                write_file(path::combine(dir, "LivelyProperties.json"), "{}");
            }
        }

        LivelyInfoModel meta;
        meta.title = c.name;
        meta.type = c.type;
        meta.is_absolute_path = c.absolute;
        meta.file_name = (c.absolute && std::string(c.file).rfind("http", 0) != 0)
                             ? path::combine(dir, c.file)
                             : std::string(c.file);
        meta.desc = "d";
        meta.author = "a";
        meta.preview = opt_or_null(c.preview);
        meta.thumbnail = opt_or_null(c.thumb);
        common::JsonStorage::StoreLivelyInfoData(path::combine(dir, "LivelyInfo.json"), meta);

        const models::LibraryModel lib = factory.create_from_directory(dir);
        line(std::string("factory.createdirectory.") + c.name, describe_library(lib));
        if (c.type == WallpaperType::app) {
            const bool exists = lib.lively_property_path.has_value() &&
                                fs::is_regular_file(*lib.lively_property_path);
            line(std::string("factory.app-props-exists.") + c.name, exists ? "file" : "<missing>");
        }
    }

    line("factory.metadata.missing", throws_name([&] {
             factory.get_metadata(path::combine(root, "nope-dir"));
         }));

    const std::string bad_json_dir = path::combine(path::combine(root, "lib"), "badjson");
    fs::create_directories(bad_json_dir);
    write_file(path::combine(bad_json_dir, "LivelyInfo.json"), "not json");
    line("factory.metadata.corrupt", throws_name([&] { factory.get_metadata(bad_json_dir); }));

    LivelyInfoModel meta_no_preview;
    meta_no_preview.title = "t";
    meta_no_preview.desc = "d";
    meta_no_preview.author = "a";
    meta_no_preview.preview = std::nullopt;
    meta_no_preview.thumbnail = "thumb.png";
    const models::LibraryModel from_meta = factory.create_from_metadata(meta_no_preview);
    line("factory.frommetadata.nopreview",
         v(from_meta.title()) + "|" + rel_path(from_meta.image_path) + "|" +
             rel_path(from_meta.file_path));

    struct PackageCase {
        const char* label;
        const char* path;
        WallpaperType type;
    };
    const PackageCase packages[] = {
        {"online", "https://example.com/gallery/wall-42", WallpaperType::url},
        {"videofile", "newvideo/my clip.mp4", WallpaperType::video},
    };
    for (const auto& p : packages) {
        const std::string dest = path::combine(path::combine(root, "packages"), p.label);
        const LivelyInfoModel meta = factory.create_wallpaper_package(p.path, dest, p.type);
        line(std::string("factory.createpackage.") + p.label, describe_info(meta));
        line(std::string("factory.createpackage.") + p.label + ".file",
             escaped_payload(read_file(path::combine(dest, "LivelyInfo.json"))));
    }

    const std::string packaged_dir = path::combine(path::combine(root, "lib"), "absapp");
    line("factory.createpackage.already-packaged", throws_name([&] {
             factory.create_wallpaper_package(path::combine(packaged_dir, "main.exe"),
                                              path::combine(path::combine(root, "packages"), "dupe"),
                                              WallpaperType::app);
         }));
}

std::string canonical_report() {
    g_report.clear();
    const std::string root = scratch_dir();
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    emit_models();
    emit_file_types();
    emit_link_util();
    emit_file_util();
    emit_wallpaper_extensions();
    emit_languages();
    emit_window_classes();
    emit_localization();
    emit_library_factory();
    return g_report;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

} // namespace

TEST_CASE("Lively.Common library transcript matches the C# oracle", "[library][oracle]") {
    const std::string expected_text = read_file(kGoldensDir + "/library_csharp.txt");
    REQUIRE_FALSE(expected_text.empty());

    const auto expected = split_lines(expected_text);
    const auto actual = split_lines(canonical_report());

    std::size_t mismatches = 0;
    const std::size_t compared = std::min(expected.size(), actual.size());
    for (std::size_t i = 0; i < compared; ++i) {
        if (expected[i] == actual[i]) continue;
        if (++mismatches <= 12) {
            INFO("line " << i + 1);
            INFO("  csharp: " << expected[i]);
            INFO("  c++   : " << actual[i]);
        }
        CHECK(expected[i] == actual[i]);
    }
    // Line count differences are reported explicitly: a missing check is as much
    // a divergence as a wrong value.
    INFO("csharp lines: " << expected.size() << ", c++ lines: " << actual.size());
    CHECK(expected.size() == actual.size());
}

TEST_CASE("wallpaper package detection matches SharpZipLib on real archives", "[library][filetypes]") {
    const std::string fixtures = kGoldensDir + "/fixtures";
    CHECK(common::is_wallpaper_package(fixtures + "/package_ok.zip"));
    CHECK(common::is_wallpaper_package(fixtures + "/package_case.zip")); // ignoreCase lookup
    CHECK_FALSE(common::is_wallpaper_package(fixtures + "/package_nested.zip"));
    CHECK_FALSE(common::is_wallpaper_package(fixtures + "/package_no_meta.zip"));
    CHECK_FALSE(common::is_wallpaper_package(fixtures + "/not_a_zip.zip"));
    CHECK_FALSE(common::is_wallpaper_package(fixtures + "/does_not_exist.zip"));
}

TEST_CASE("System.IO.Path primitives", "[library][path]") {
    using common::path::combine;
    using common::path::get_directory_name;
    using common::path::get_extension;
    using common::path::get_file_name;
    using common::path::get_file_name_without_extension;

    CHECK(combine("C:\\a", "b.txt") == "C:\\a\\b.txt");
    CHECK(combine("C:\\a\\", "b.txt") == "C:\\a\\b.txt");
    CHECK(combine("C:\\a", "D:\\b") == "D:\\b"); // rooted second argument wins
    CHECK(combine("", "b.txt") == "b.txt");

    CHECK(get_extension("a/b.txt") == ".txt");
    CHECK(get_extension("a/b") == "");
    CHECK(get_extension("a.") == "");           // trailing dot is not an extension
    CHECK(get_extension(".gitignore") == ".gitignore");
    CHECK(get_file_name("a/b/c.txt") == "c.txt");
    CHECK(get_file_name("c.txt") == "c.txt");
    CHECK(get_file_name_without_extension("a/my clip.mp4") == "my clip");
    CHECK(get_file_name_without_extension("a/.gitignore") == "");

    REQUIRE(get_directory_name("C:\\dir\\file.txt").has_value());
    CHECK(*get_directory_name("C:\\dir\\file.txt") == "C:\\dir");
    REQUIRE(get_directory_name("C:\\file.txt").has_value());
    CHECK(*get_directory_name("C:\\file.txt") == "C:\\"); // drive roots keep the separator
    CHECK_FALSE(get_directory_name("file.txt").has_value());
    CHECK_FALSE(get_directory_name("").has_value());
}

TEST_CASE("LivelyInfoModel survives a JSON round-trip", "[library][models]") {
    LivelyInfoModel original;
    original.title = "Ünïcode ✓ \"quoted\"";
    original.preview = "";
    original.thumbnail = std::nullopt;
    original.type = WallpaperType::webaudio;
    original.arguments = "--flag";
    original.is_absolute_path = true;
    original.tags = std::vector<std::string>{"a", "b"};
    original.version = 3;

    const LivelyInfoModel parsed = LivelyInfoModel::from_json_string(original.to_json_string());
    CHECK(parsed.title == original.title);
    CHECK(parsed.preview == original.preview);          // "" survives as ""
    CHECK(parsed.thumbnail == std::nullopt);            // null survives as null
    CHECK(parsed.type == original.type);
    CHECK(parsed.arguments == original.arguments);
    CHECK(parsed.is_absolute_path);
    CHECK(parsed.tags == original.tags);
    CHECK(parsed.version == 3);

    // The C# copy constructor re-reads AppVersion from the executing assembly.
    LivelyInfoModel stale = original;
    stale.app_version = "9.9.9.9";
    CHECK(LivelyInfoModel::copy(stale).app_version == models::kLivelyInfoAppVersion);

    // An absent Type keeps the constructor default (web), not app.
    const LivelyInfoModel sparse = LivelyInfoModel::from_json_string("{}");
    CHECK(sparse.type == WallpaperType::web);
    CHECK(sparse.app_version == models::kLivelyInfoAppVersion);
}

TEST_CASE("languages and desktop classes are usable data", "[library]") {
    const auto& languages = common::supported_languages();
    CHECK(languages.size() == 46);
    CHECK(languages.front().code == "en-US");
    CHECK(languages.front().display_name == "English");
    // Every code is a plausible BCP-47 tag with a region subtag, as upstream.
    for (const auto& language : languages) {
        CHECK(language.code.find('-') != std::string::npos);
        CHECK_FALSE(language.display_name.empty());
    }
    CHECK(common::is_desktop_class("WorkerW"));
    CHECK(common::is_desktop_class("progman"));
    CHECK_FALSE(common::is_desktop_class("Notepad"));
}

TEST_CASE("size_suffix rejects a negative decimal count", "[library][fileutil]") {
    CHECK_THROWS_AS(common::size_suffix(1024, -1), std::out_of_range);
}
