#pragma once
// Port of Lively.UI.Shared/Factories/AppThemeFactory.cs (the IAppThemeFactory
// implementation). Small, but it is where the on-disk theme layout is defined,
// so it belongs with the core rather than with the (deferred) UI.
//
// C# behaviour, including the parts that look like mistakes:
//
//   CreateFromFile(filePath, name, description):
//     1. themeDir = ThemeDir/<Path.GetRandomFileName()>  (a random 8.3 name)
//     2. copy the source file in
//     3. `theme` holds the ABSOLUTE copied path for File and Preview
//     4. theme.json is written with `new ThemeModel(theme) { File =
//        Path.GetFileName(theme.File), ... }` — i.e. RELATIVE filenames
//     5. the ABSOLUTE-path `theme` is returned
//   So the caller gets an absolute path while the persisted file is relative to
//   the theme directory. Resolving on read is what CreateFromDirectory does, and
//   the asymmetry is deliberate (the theme directory can be moved).
//
//   CreateFromDirectory(themeDir):
//     * throws FileNotFoundException when theme.json is missing
//     * returns a copy with File/Preview re-rooted at themeDir and
//       IsEditable = true (the runtime-only flag, which is [JsonIgnore]).
//
// `File.Copy(filePath, Path.Combine(themeDir, copyFile))` is the C# line worth
// reading twice: `copyFile` is already absolute, and `Path.Combine` discards the
// first argument when the second is rooted — so the copy target is `copyFile`
// itself, not `themeDir\copyFile`. The port relies on the same Combine rule.

#include <lively/models/theme.h>

#include <string>

namespace lively::common::factories {

class AppThemeFactory {
public:
    // ThemeDir()/  — injectable so tests do not touch the user's real themes.
    explicit AppThemeFactory(std::string theme_dir);

    // Defaults to Constants.CommonPaths.ThemeDir().
    AppThemeFactory();

    // Throws std::runtime_error when the source file is missing or the copy
    // fails (C# surfaces IOException/UnauthorizedAccessException from File.Copy).
    models::ThemeModel CreateFromFile(const std::string& file_path, const std::string& name,
                                      const std::string& description);

    // Throws std::runtime_error (C# FileNotFoundException) when theme.json is
    // absent, and on a corrupt/unreadable file (C# JsonReaderException).
    models::ThemeModel CreateFromDirectory(const std::string& theme_dir);

private:
    std::string theme_dir_;
};

} // namespace lively::common::factories
