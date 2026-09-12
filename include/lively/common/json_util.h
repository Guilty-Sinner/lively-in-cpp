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

#include <lively/models/ipc_message.h>
#include <lively/models/settings_model.h>

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
    // C# Write(path, JObject): indented formatting (JObject.ToString default).
    static void Write(const std::string& path, const JsonValue& value) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open file: " + path);
        out << value.dump(2) << '\n';
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

private:
    static std::string read_all(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open file: " + path);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

} // namespace lively::common
