#pragma once
// Port of the remaining small Lively.Models types that Lively.Common builds on:
//
//   LibraryModel.cs      — a library tile (the MVVM ObservableObject becomes a
//                          plain value type; see the note below)
//   FileTypeModel.cs     — (WallpaperType, extensions) row of FileTypes.SupportedFormats
//   LanguageModel.cs     — (DisplayName, Code) row of Languages.SupportedLanguages
//
// LibraryModel is `ObservableObject` with [ObservableProperty] backing fields, so
// C# notifies INotifyPropertyChanged on assignment. The core has no UI thread and
// no data-binding, so the port keeps the *state* semantics and drops the change
// notifications; the property *setter* semantics that callers depend on are kept
// exactly: Title/Author/Desc coerce null-or-whitespace to "---".

#include <lively/models/lively_info.h>
#include <lively/models/wallpaper_type.h>

#include <optional>
#include <string>
#include <vector>

namespace lively::models {

struct LibraryModel {
    bool is_subscribed = false;
    bool is_downloading = false;
    bool is_ready_to_set = true;
    float downloading_progress = 0.0f;
    std::string downloading_progress_text = "-/- MB";
    LivelyInfoModel lively_info;
    std::optional<std::string> file_path;
    std::optional<std::string> lively_info_folder_path;
    std::optional<std::string> lively_info_localization_path;
    std::optional<std::string> lively_property_localization_path;
    std::optional<std::string> image_path;
    std::optional<std::string> preview_clip_path;
    std::optional<std::string> thumbnail_path;
    std::optional<std::string> lively_property_path;

    // C# `Title`/`Author`/`Desc`: null or whitespace becomes "---".
    const std::string& title() const { return title_; }
    void set_title(std::optional<std::string> value) { title_ = coerce(std::move(value)); }
    const std::string& author() const { return author_; }
    void set_author(std::optional<std::string> value) { author_ = coerce(std::move(value)); }
    const std::string& desc() const { return desc_; }
    void set_desc(std::optional<std::string> value) { desc_ = coerce(std::move(value)); }

private:
    static std::string coerce(std::optional<std::string> value) {
        if (!value.has_value()) return "---";
        const std::string& v = *value;
        if (v.find_first_not_of(" \t\r\n\f\v") == std::string::npos) return "---";
        return v;
    }

    std::string title_ = "---";
    std::string author_ = "---";
    std::string desc_ = "---";
};

// Port of Lively.Models/FileTypeModel.cs.
struct FileTypeModel {
    WallpaperType type;
    std::vector<std::string> extensions; // C# field name is `Extentions` [sic]
};

// Port of Lively.Models/LanguageModel.cs.
struct LanguageModel {
    std::string display_name;
    std::string code;
};

} // namespace lively::models
