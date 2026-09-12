#include <lively/models/lively_info.h>

#include <lively/common/json_format.h>

namespace lively::models {

namespace {

using json = nlohmann::ordered_json;

std::optional<std::string> read_opt_string(const json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    if (j.at(key).is_string()) return j.at(key).get<std::string>();
    // Newtonsoft coerces scalars to string; keep that behaviour.
    return json(j.at(key)).dump();
}

std::optional<std::vector<std::string>> read_opt_tags(const json& j) {
    if (!j.contains("Tags") || j.at("Tags").is_null()) return std::nullopt;
    std::vector<std::string> tags;
    const auto& arr = j.at("Tags");
    if (arr.is_array()) {
        for (const auto& item : arr) {
            tags.push_back(item.is_string() ? item.get<std::string>() : json(item).dump());
        }
    }
    return tags;
}

} // namespace

json LivelyInfoModel::to_json() const {
    json j;
    j["AppVersion"] = app_version;
    // C# null string → JSON null (NullValueHandling.Include).
    j["Title"] = title.has_value() ? json(*title) : json(nullptr);
    j["Thumbnail"] = thumbnail.has_value() ? json(*thumbnail) : json(nullptr);
    j["Preview"] = preview.has_value() ? json(*preview) : json(nullptr);
    j["Desc"] = desc.has_value() ? json(*desc) : json(nullptr);
    j["Author"] = author.has_value() ? json(*author) : json(nullptr);
    j["License"] = license.has_value() ? json(*license) : json(nullptr);
    j["Contact"] = contact.has_value() ? json(*contact) : json(nullptr);
    j["Type"] = static_cast<int>(type);
    j["FileName"] = file_name.has_value() ? json(*file_name) : json(nullptr);
    j["Arguments"] = arguments.has_value() ? json(*arguments) : json(nullptr);
    j["IsAbsolutePath"] = is_absolute_path;
    j["Id"] = id.has_value() ? json(*id) : json(nullptr);
    if (tags.has_value()) {
        json arr = json::array();
        for (const auto& t : *tags) arr.push_back(t);
        j["Tags"] = std::move(arr);
    } else {
        j["Tags"] = json(nullptr);
    }
    j["Version"] = version;
    return j;
}

std::string LivelyInfoModel::to_json_string() const {
    return common::newtonsoft_indented(to_json());
}

LivelyInfoModel LivelyInfoModel::from_json_string(const std::string& text) {
    const json j = json::parse(text);

    // Newtonsoft constructs the object first (running the parameterless ctor,
    // which sets AppVersion and Type = web) and only then applies the members
    // present in the document. So an absent "Type" keeps web rather than
    // defaulting to app — verified by the oracle's `livelyinfo.sparse` line.
    LivelyInfoModel m;
    if (j.is_null()) return m;

    if (j.contains("AppVersion") && !j.at("AppVersion").is_null()) {
        m.app_version = j.at("AppVersion").is_string() ? j.at("AppVersion").get<std::string>()
                                                       : json(j.at("AppVersion")).dump();
    }
    m.title = read_opt_string(j, "Title");
    m.thumbnail = read_opt_string(j, "Thumbnail");
    m.preview = read_opt_string(j, "Preview");
    m.desc = read_opt_string(j, "Desc");
    m.author = read_opt_string(j, "Author");
    m.license = read_opt_string(j, "License");
    m.contact = read_opt_string(j, "Contact");
    if (j.contains("Type") && j.at("Type").is_number_integer()) {
        m.type = static_cast<WallpaperType>(j.at("Type").get<int>());
    }
    m.file_name = read_opt_string(j, "FileName");
    m.arguments = read_opt_string(j, "Arguments");
    if (j.contains("IsAbsolutePath") && j.at("IsAbsolutePath").is_boolean()) {
        m.is_absolute_path = j.at("IsAbsolutePath").get<bool>();
    }
    m.id = read_opt_string(j, "Id");
    m.tags = read_opt_tags(j);
    if (j.contains("Version") && j.at("Version").is_number_integer()) {
        m.version = j.at("Version").get<int>();
    }
    return m;
}

LivelyInfoModel LivelyInfoModel::copy(const LivelyInfoModel& source) {
    LivelyInfoModel m = source;
    m.app_version = kLivelyInfoAppVersion;
    return m;
}

LivelyInfoLocalizationFile LivelyInfoLocalizationFile::from_json_string(const std::string& text) {
    LivelyInfoLocalizationFile file;
    const json j = json::parse(text);
    if (!j.is_object() || !j.contains("Languages") || !j.at("Languages").is_object()) {
        return file;
    }
    for (const auto& [code, value] : j.at("Languages").items()) {
        file.languages.emplace_back(code, LivelyInfoModel::from_json_string(value.dump()));
    }
    return file;
}

const LivelyInfoModel* LivelyInfoLocalizationFile::find(const std::string& code) const {
    for (const auto& [key, value] : languages) {
        if (key == code) return &value;
    }
    return nullptr;
}

} // namespace lively::models
