#pragma once
// Newtonsoft.Json's `Formatting.Indented` byte shape, shared by the models that
// are persisted through Lively.Common's JsonStorage<T> (JsonStorage.StoreData)
// and by JsonUtil.Write (JObject.ToString()).
//
// Two details the golden fixtures pin (tests/goldens/library_csharp.txt):
//   * line breaks are Environment.NewLine — CRLF on Windows, so the files the
//     port writes are byte-identical to the C# originals on the same OS;
//   * there is NO trailing newline (StreamWriter + JsonTextWriter, and
//     File.WriteAllText(path, JObject.ToString())).
// Indentation (2 spaces), ": " separators, array layout and string escaping are
// exactly nlohmann's dump(2) with ensure_ascii=false.

#include <nlohmann/json.hpp>

#include <string>

namespace lively::common {

inline std::string newtonsoft_indented(const nlohmann::ordered_json& value) {
    const std::string pretty = value.dump(2);
    std::string out;
    out.reserve(pretty.size() + pretty.size() / 8);
    for (const char c : pretty) {
        if (c == '\n') {
            out += "\r\n";
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace lively::common
