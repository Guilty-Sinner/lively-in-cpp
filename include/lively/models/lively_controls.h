#pragma once
// Port of the LivelyProperty pipeline:
//   Lively.Common/Helpers/LivelyPropertyUtil.cs      (load/localize driver)
//   Lively.Common/JsonConverters/LivelyControlModelConverter.cs (type dispatch)
//   Lively.Models/LivelyControls/*.cs                (the control models)
//
// LivelyProperties.json maps key -> control object; the JSON object "type"
// field selects the concrete model; a control without a "name" takes the JSON
// key as its name (reader.Path.Split('.').Last() in C#).
//
// C# value extraction (LoadProperty): Button/Label are user-interaction-only
// and skipped; every other control yields its Value (slider: double,
// dropdown/scalerDropdown: int, checkbox: bool, textbox/color: string,
// folderDropdown: relative file path if it exists under rootDir).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace lively::models {

class ControlModel {
public:
    explicit ControlModel(std::string type) : type(std::move(type)) {}
    virtual ~ControlModel() = default;

    std::string name;  // JSON "name" or the property-file key
    std::string text;  // JSON "text"
    std::string type;  // fixed per class (lowercase matches converter dispatch)
    std::string help;  // JSON "help"

    // Value extraction per C# LoadProperty switch. Button/Label return false
    // (skipped); others write their value as JSON (double/int/bool/string).
    virtual bool try_get_value(nlohmann::ordered_json& out) const = 0;
};

#define LIVELY_CONTROL(CLASS, TYPE_STR)                    \
    CLASS() : ControlModel(TYPE_STR) {}

class SliderModel final : public ControlModel {
public:
    LIVELY_CONTROL(SliderModel, "slider")
    int tick = 0;        // JsonIgnore in C# but still deserialized via "tick"
    double min = 0.0;
    double max = 0.0;
    double value = 0.0;
    double step = 1.0;   // C# default 1 (prevents div-by-zero)
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value; return true;
    }
};

class TextboxModel final : public ControlModel {
public:
    LIVELY_CONTROL(TextboxModel, "textbox")
    std::string value;  // C# null <-> std::nullopt
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(value);
        return true;
    }
};

class DropdownModel final : public ControlModel {
public:
    LIVELY_CONTROL(DropdownModel, "dropdown")
    int value = 0;
    std::vector<std::string> items;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value; return true;
    }
};

class FolderDropdownModel final : public ControlModel {
public:
    LIVELY_CONTROL(FolderDropdownModel, "folderDropdown")
    std::string value;
    std::string folder;
    std::string filter;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(value);
        return true;
    }
};

class ScalerDropdownModel final : public ControlModel {
public:
    LIVELY_CONTROL(ScalerDropdownModel, "scalerDropdown")
    int value = 0;
    std::vector<std::string> items;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value; return true;
    }
};

class ButtonModel final : public ControlModel {
public:
    LIVELY_CONTROL(ButtonModel, "button")
    std::string value;
    bool try_get_value(nlohmann::ordered_json&) const override { return false; }
};

class ColorPickerModel final : public ControlModel {
public:
    LIVELY_CONTROL(ColorPickerModel, "color")
    std::string value;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(value);
        return true;
    }
};

class CheckboxModel final : public ControlModel {
public:
    LIVELY_CONTROL(CheckboxModel, "checkbox")
    bool value = false;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value; return true;
    }
};

class LabelModel final : public ControlModel {
public:
    LIVELY_CONTROL(LabelModel, "label")
    std::string value;
    bool try_get_value(nlohmann::ordered_json& out) const override {
        out = value.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(value);
        return true;
    }
};

#undef LIVELY_CONTROL

using ControlMap = std::map<std::string, std::unique_ptr<ControlModel>>;

class LivelyPropertyUtil {
public:
    // LivelyControlModelConverter.ReadJson: parse one control object.
    // Throws std::runtime_error on unsupported type (like NotSupportedException).
    // json_key supplies the fallback name (the property-file key).
    static std::unique_ptr<ControlModel> ParseControl(const nlohmann::ordered_json& j,
                                                      const std::string& json_key);

    // GetControls: whole LivelyProperties.json file (keys preserved).
    static ControlMap GetControls(const nlohmann::ordered_json& root);

    // File convenience wrapper.
    static ControlMap GetControlsFromFile(const std::string& path);

    // LivelyPropertyUtil.LocalizeControls: apply LocalizationFile overrides
    // (exact language match, then base-language fallback). Mutates controls.
    static void LocalizeControls(const nlohmann::ordered_json& loc_root,
                                 ControlMap& controls,
                                 const std::string& language_code);
};

// Canonical single-line description used for oracle comparison against the C#
// probe: deterministic field order, invariant-culture numbers.
std::string describe_control(const ControlModel& control);

} // namespace lively::models
