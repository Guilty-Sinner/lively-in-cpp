#include "lively/models/lively_controls.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lively::models {

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string json_to_string(const nlohmann::ordered_json& j) {
    if (j.is_null()) return {};
    if (j.is_string()) return j.get<std::string>();
    return j.dump(); // numbers/bools as text (C# Convert.ToString equivalent)
}

std::vector<std::string> json_to_strings(const nlohmann::ordered_json& j) {
    std::vector<std::string> out;
    if (j.is_array()) {
        for (const auto& item : j) out.push_back(item.is_string() ? item.get<std::string>() : item.dump());
    }
    return out;
}

template <typename T>
std::unique_ptr<ControlModel> make_control(const nlohmann::ordered_json& j) {
    auto control = std::make_unique<T>();
    if (auto* base = dynamic_cast<ControlModel*>(control.get())) {
        base->text = json_to_string(j.value("text", nlohmann::ordered_json(nullptr)));
        base->help = json_to_string(j.value("help", nlohmann::ordered_json(nullptr)));
    }
    if constexpr (std::is_same_v<T, SliderModel>) {
        // NOTE: Tick is [JsonIgnore] in C# — Newtonsoft skips it even though a
        // [JsonProperty("tick")] also exists (ignore wins). Verified against
        // the C# oracle: LivelyProperties.json has "tick": 200 but Tick stays 0.
        control->min = j.value("min", 0.0);
        control->max = j.value("max", 0.0);
        control->value = j.value("value", 0.0);
        control->step = j.value("step", 1.0);
    } else if constexpr (std::is_same_v<T, TextboxModel>) {
        control->value = json_to_string(j.value("value", nlohmann::ordered_json(nullptr)));
    } else if constexpr (std::is_same_v<T, DropdownModel> || std::is_same_v<T, ScalerDropdownModel>) {
        control->value = j.value("value", 0);
        control->items = json_to_strings(j.value("items", nlohmann::ordered_json(nullptr)));
    } else if constexpr (std::is_same_v<T, FolderDropdownModel>) {
        control->value = json_to_string(j.value("value", nlohmann::ordered_json(nullptr)));
        control->folder = json_to_string(j.value("folder", nlohmann::ordered_json(nullptr)));
        control->filter = json_to_string(j.value("filter", nlohmann::ordered_json(nullptr)));
    } else if constexpr (std::is_same_v<T, CheckboxModel>) {
        const auto v = j.value("value", nlohmann::ordered_json(nullptr));
        if (v.is_boolean()) control->value = v.get<bool>();
    } else if constexpr (std::is_same_v<T, ButtonModel> || std::is_same_v<T, ColorPickerModel> ||
                         std::is_same_v<T, LabelModel>) {
        control->value = json_to_string(j.value("value", nlohmann::ordered_json(nullptr)));
    }
    return control;
}

// LocalizedStrings port: { text, value, help, items }.
struct LocalizedStrings {
    std::string text;
    std::string value;
    std::string help;
    std::vector<std::string> items;

    static LocalizedStrings parse(const nlohmann::ordered_json& j) {
        LocalizedStrings s;
        s.text = json_to_string(j.value("text", nlohmann::ordered_json(nullptr)));
        s.value = json_to_string(j.value("value", nlohmann::ordered_json(nullptr)));
        s.help = json_to_string(j.value("help", nlohmann::ordered_json(nullptr)));
        s.items = json_to_strings(j.value("items", nlohmann::ordered_json(nullptr)));
        return s;
    }

    bool is_blank(const std::string& s) const {
        return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    }
};

} // namespace

std::unique_ptr<ControlModel> LivelyPropertyUtil::ParseControl(
    const nlohmann::ordered_json& j, const std::string& json_key) {
    const std::string type = to_lower(json_to_string(j.value("type", nlohmann::ordered_json(nullptr))));

    std::unique_ptr<ControlModel> control;
    if (type == "slider") control = make_control<SliderModel>(j);
    else if (type == "textbox") control = make_control<TextboxModel>(j);
    else if (type == "dropdown") control = make_control<DropdownModel>(j);
    else if (type == "folderdropdown") control = make_control<FolderDropdownModel>(j);
    else if (type == "scalerdropdown") control = make_control<ScalerDropdownModel>(j);
    else if (type == "button") control = make_control<ButtonModel>(j);
    else if (type == "color") control = make_control<ColorPickerModel>(j);
    else if (type == "checkbox") control = make_control<CheckboxModel>(j);
    else if (type == "label") control = make_control<LabelModel>(j);
    else throw std::runtime_error("Control type '" + type + "' is not supported.");

    // C#: control.Name = reader.Path.Split('.').Last() when name missing.
    control->name = json_to_string(j.value("name", nlohmann::ordered_json(nullptr)));
    if (control->name.empty()) control->name = json_key;

    return control;
}

ControlMap LivelyPropertyUtil::GetControls(const nlohmann::ordered_json& root) {
    ControlMap out;
    if (!root.is_object()) return out;
    for (auto it = root.begin(); it != root.end(); ++it) {
        out[it.key()] = ParseControl(it.value(), it.key());
    }
    return out;
}

ControlMap LivelyPropertyUtil::GetControlsFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    const auto root = nlohmann::ordered_json::parse(in, nullptr, true, true);
    return GetControls(root);
}

void LivelyPropertyUtil::LocalizeControls(const nlohmann::ordered_json& loc_root,
                                          ControlMap& controls,
                                          const std::string& language_code) {
    if (!loc_root.is_object()) return;
    const auto languages_it = loc_root.find("Languages");
    if (languages_it == loc_root.end() || !languages_it->is_object()) return;

    // Exact match (zh-CN), then base-language fallback (zh).
    auto lang_it = languages_it->find(language_code);
    if (lang_it == languages_it->end()) {
        const auto dash = language_code.find('-');
        if (dash != std::string::npos) {
            lang_it = languages_it->find(language_code.substr(0, dash));
        }
        if (lang_it == languages_it->end()) return;
    }

    for (auto it = lang_it->begin(); it != lang_it->end(); ++it) {
        const auto control_it = controls.find(it.key());
        if (control_it == controls.end()) continue;
        ControlModel& control = *control_it->second;
        const LocalizedStrings value = LocalizedStrings::parse(it.value());

        if (control.type == "dropdown" || control.type == "scalerDropdown") {
            // IDropdownItem branch.
            if (auto* dropdown = dynamic_cast<DropdownModel*>(&control)) {
                const auto count = std::min(dropdown->items.size(), value.items.size());
                for (std::size_t i = 0; i < count; ++i) dropdown->items[i] = value.items[i];
            } else if (auto* scaler = dynamic_cast<ScalerDropdownModel*>(&control)) {
                const auto count = std::min(scaler->items.size(), value.items.size());
                for (std::size_t i = 0; i < count; ++i) scaler->items[i] = value.items[i];
            }
        } else if (control.type == "label") {
            if (!value.is_blank(value.value)) {
                if (auto* label = dynamic_cast<LabelModel*>(&control)) label->value = value.value;
            }
        } else if (control.type == "button") {
            if (!value.is_blank(value.value)) {
                if (auto* button = dynamic_cast<ButtonModel*>(&control)) button->value = value.value;
            }
        }

        if (!value.is_blank(value.text)) control.text = value.text;
        if (!value.is_blank(value.help)) control.help = value.help;
    }
}

std::string describe_control(const ControlModel& control) {
    // Deterministic oracle line: fields in C# declaration order.
    std::ostringstream out;
    out << control.type << "|name=" << control.name << "|text=" << control.text
        << "|help=" << control.help;
    if (const auto* s = dynamic_cast<const SliderModel*>(&control)) {
        out << "|tick=" << s->tick << "|min=" << s->min << "|max=" << s->max
            << "|value=" << s->value << "|step=" << s->step;
    } else if (const auto* d = dynamic_cast<const DropdownModel*>(&control)) {
        out << "|value=" << d->value << "|items=" << d->items.size();
    } else if (const auto* sd = dynamic_cast<const ScalerDropdownModel*>(&control)) {
        out << "|value=" << sd->value << "|items=" << sd->items.size();
    } else if (const auto* f = dynamic_cast<const FolderDropdownModel*>(&control)) {
        out << "|value=" << f->value << "|folder=" << f->folder << "|filter=" << f->filter;
    } else if (const auto* b = dynamic_cast<const CheckboxModel*>(&control)) {
        out << "|value=" << (b->value ? "True" : "False");
    } else if (const auto* t = dynamic_cast<const TextboxModel*>(&control)) {
        out << "|value=" << t->value;
    } else if (const auto* c = dynamic_cast<const ColorPickerModel*>(&control)) {
        out << "|value=" << c->value;
    } else if (const auto* l = dynamic_cast<const LabelModel*>(&control)) {
        out << "|value=" << l->value;
    } else if (const auto* b = dynamic_cast<const ButtonModel*>(&control)) {
        out << "|value=" << b->value;
    }
    return out.str();
}

} // namespace lively::models
