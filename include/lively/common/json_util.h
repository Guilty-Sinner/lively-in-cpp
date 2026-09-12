#pragma once
// Port of Lively.Common/Helpers/Storage/JsonUtil.cs + JsonStorage.cs.
//
// C# JsonUtil:
//   Write(path, JObject)   — JObject.ToString() == INDENTED formatting
//   ReadJObject(path)      — JObject.Parse(File.ReadAllText)
//   ReadJToken(path)       — JToken.Parse(...)
//   Serialize(obj)         — JsonConvert.SerializeObject (COMPACT)
// C# JsonStorage<T>:
//   LoadData(path)         — typed deserialize; corrupted file → exception
//   StoreData(path, data)  — typed serialize, Formatting.Indented,
//                            NullValueHandling.Include
//
// C++ mapping: nlohmann::ordered_json (indented dump with 2-space pad is
// byte-compatible with Newtonsoft's Formatting.Indented output shape; the
// golden tests pin the exact bytes for the IPC/settings payloads).

#include <lively/common/base64.h>
#include <lively/common/json_format.h>
#include <lively/models/application_rules.h>
#include <lively/models/ipc_message.h>
#include <lively/models/lively_info.h>
#include <lively/models/settings_model.h>
#include <lively/models/theme.h>
#include <lively/models/wallpaper_layout.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace lively::common {

using JsonValue = nlohmann::ordered_json;

class JsonUtil {
public:
    // C# Write(path, JObject) — File.WriteAllText(path, rss.ToString()):
    // JObject.ToString() is Formatting.Indented, so CRLF line breaks and NO
    // trailing newline (both pinned by the `jsonutil.write` oracle line).
    static void Write(const std::string& path, const JsonValue& value) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << newtonsoft_indented(value);
    }

    static JsonValue ReadJObject(const std::string& path) {
        return JsonValue::parse(read_all(path));
    }

    static JsonValue ReadJToken(const std::string& path) {
        return JsonValue::parse(read_all(path));
    }

    // C# Serialize: compact, no spaces (JsonConvert.SerializeObject).
    static std::string Serialize(const JsonValue& value) {
        return value.dump();
    }

    // Typed convenience matching the C# call-sites that pass IPC messages /
    // settings models (models::serialize is the Newtonsoft-identical encoder).
    static std::string Serialize(const models::IpcMessage& msg) {
        return models::serialize(msg);
    }

    static std::string Serialize(const models::SettingsModel& settings) {
        return settings.to_json_string();
    }

private:
    static std::string read_all(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open file: " + path);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

// C# JsonStorage<T> — the T-dispatch becomes explicit overloads for the ported
// models (the C# callers use exactly these types).
class JsonStorage {
public:
    // C# LoadData: corrupted/empty file → exception (ArgumentNullException).
    static models::SettingsModel LoadData(const std::string& path) {
        return models::SettingsModel::from_json_string(read_all(path));
    }

    // C# StoreData: Formatting.Indented + NullValueHandling.Include.
    static void StoreData(const std::string& path, const models::SettingsModel& settings) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << settings.to_json_string();
    }

    // C# JsonStorage<LivelyInfoModel>.LoadData / .StoreData. C++ cannot overload
    // on return type, so the generic argument is spelled out in the name.
    static models::LivelyInfoModel LoadLivelyInfoData(const std::string& path) {
        const std::string text = read_all(path);
        try {
            return models::LivelyInfoModel::from_json_string(text);
        } catch (const std::exception&) {
            // C# surfaces JsonReaderException; callers only branch on failure.
            throw std::runtime_error("json null/corrupt");
        }
    }

    static void StoreLivelyInfoData(const std::string& path, const models::LivelyInfoModel& info) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << info.to_json_string();
    }

    // C# JsonStorage<List<WallpaperLayoutModel>> / <List<ScreenSaverLayoutModel>>.
    // Both files are a JSON array at the root, indented the same way.
    static std::vector<models::WallpaperLayoutModel> LoadWallpaperLayoutData(
        const std::string& path) {
        const JsonValue array = JsonValue::parse(read_all(path));
        std::vector<models::WallpaperLayoutModel> layouts;
        if (!array.is_array()) throw std::runtime_error("json null/corrupt");
        for (const auto& item : array) {
            layouts.push_back(models::WallpaperLayoutModel::from_json(item));
        }
        return layouts;
    }

    static void StoreWallpaperLayoutData(
        const std::string& path, const std::vector<models::WallpaperLayoutModel>& layouts) {
        JsonValue array = JsonValue::array();
        for (const auto& layout : layouts) array.push_back(layout.to_json());
        write_indented(path, array);
    }

    static std::vector<models::ScreenSaverLayoutModel> LoadScreenSaverLayoutData(
        const std::string& path) {
        const JsonValue array = JsonValue::parse(read_all(path));
        std::vector<models::ScreenSaverLayoutModel> layouts;
        if (!array.is_array()) throw std::runtime_error("json null/corrupt");
        for (const auto& item : array) {
            layouts.push_back(models::ScreenSaverLayoutModel::from_json(item));
        }
        return layouts;
    }

    static void StoreScreenSaverLayoutData(
        const std::string& path, const std::vector<models::ScreenSaverLayoutModel>& layouts) {
        JsonValue array = JsonValue::array();
        for (const auto& layout : layouts) array.push_back(layout.to_json());
        write_indented(path, array);
    }

    // C# JsonStorage<ThemeModel> — an installed theme's theme.json.
    static models::ThemeModel LoadThemeData(const std::string& path) {
        return models::ThemeModel::from_json(parse_json(path));
    }

    static void StoreThemeData(const std::string& path, const models::ThemeModel& theme) {
        write_indented(path, theme.to_json());
    }

    // C# JsonStorage<byte[]> — the outer container of EncryptUtil<T>, so
    // Tokens.dat is a JSON *string* of base64 (see the `jsonstorage.bytes` and
    // `jsonstorage.bytes.empty` oracle lines: an empty array is `""`, not null).
    static std::vector<unsigned char> LoadBytesData(const std::string& path) {
        const JsonValue parsed = parse_json(path);
        if (!parsed.is_string()) throw std::runtime_error("json null/corrupt");
        const auto decoded = base64_decode(parsed.get<std::string>());
        if (!decoded) throw std::runtime_error("invalid base64 in " + path);
        return *decoded;
    }

    static void StoreBytesData(const std::string& path, const std::vector<unsigned char>& data) {
        write_raw(path, JsonValue(base64_encode(data)).dump());
    }

    // C# JsonStorage<List<ApplicationRulesModel>> (AppRules.json).
    static std::vector<models::ApplicationRulesModel> LoadAppRulesData(const std::string& path) {
        std::vector<models::ApplicationRulesModel> rules;
        for (const auto& item : parse_or_throw(path)) {
            rules.push_back(models::ApplicationRulesModel::from_json(item));
        }
        return rules;
    }

    static void StoreAppRulesData(const std::string& path,
                                  const std::vector<models::ApplicationRulesModel>& rules) {
        JsonValue array = JsonValue::array();
        for (const auto& rule : rules) array.push_back(rule.to_json());
        write_indented(path, array);
    }

    // C# JsonStorage<List<AppMusicExclusionRuleModel>> (MusicAppExclusionRules.json).
    static std::vector<models::AppMusicExclusionRuleModel> LoadMusicExclusionRulesData(
        const std::string& path) {
        std::vector<models::AppMusicExclusionRuleModel> rules;
        for (const auto& item : parse_or_throw(path)) {
            rules.push_back(models::AppMusicExclusionRuleModel::from_json(item));
        }
        return rules;
    }

    static void StoreMusicExclusionRulesData(
        const std::string& path, const std::vector<models::AppMusicExclusionRuleModel>& rules) {
        JsonValue array = JsonValue::array();
        for (const auto& rule : rules) array.push_back(rule.to_json());
        write_indented(path, array);
    }

private:
    // Newtonsoft throws a JsonReaderException on unparseable input; the port
    // reports the same via an exception (every caller branches on failure).
    static JsonValue parse_json(const std::string& path) {
        try {
            return JsonValue::parse(read_all(path));
        } catch (const std::exception&) {
            throw std::runtime_error("json null/corrupt");
        }
    }

    // The List<T>-rooted stores additionally require a JSON array at the root.
    static JsonValue parse_or_throw(const std::string& path) {
        JsonValue parsed = parse_json(path);
        if (!parsed.is_array()) throw std::runtime_error("json null/corrupt");
        return parsed;
    }

    static void write_raw(const std::string& path, const std::string& contents) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << contents;
    }

    static void write_indented(const std::string& path, const JsonValue& value) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << newtonsoft_indented(value);
    }

    static std::string read_all(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open file: " + path);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

} // namespace lively::common
