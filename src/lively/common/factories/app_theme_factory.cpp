#include <lively/common/factories/app_theme_factory.h>

#include <lively/common/constants.h>
#include <lively/common/json_util.h>
#include <lively/common/path_util.h>

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace lively::common::factories {

namespace {

namespace fs = std::filesystem;

// Path.GetRandomFileName(): an 8.3 name, cryptographically random on .NET. The
// port uses std::random_device seeded mt19937 — not cryptographic, which is fine
// here: the value only has to avoid collisions in the theme directory, and it is
// documented as such because the C# name is intentionally unguessable.
//
// Consequence worth noting: the generated directory name is NON-deterministic,
// so the oracle pins the theme.json *bytes* and the read-back model (with the
// directory substituted), never the created directory.
std::string random_file_name() {
    static constexpr const char* kLower = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<std::size_t> pick(0, 35);

    std::string name;
    name.reserve(12);
    for (int i = 0; i < 8; ++i) name.push_back(kLower[pick(generator)]);
    name.push_back('.');
    for (int i = 0; i < 3; ++i) name.push_back(kLower[pick(generator)]);
    return name;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec);
}

} // namespace

AppThemeFactory::AppThemeFactory() : AppThemeFactory(path::utf8_from_wide(common_paths::ThemeDir())) {}

AppThemeFactory::AppThemeFactory(std::string theme_dir) : theme_dir_(std::move(theme_dir)) {}

models::ThemeModel AppThemeFactory::CreateFromFile(const std::string& file_path,
                                                   const std::string& name,
                                                   const std::string& description) {
    const std::string theme_dir = path::combine(theme_dir_, random_file_name());
    std::error_code ec;
    fs::create_directories(fs::u8path(theme_dir), ec);
    if (ec) throw std::runtime_error("cannot create theme directory: " + theme_dir);

    // `Path.Combine(themeDir, Path.GetFileName(filePath))` — and then C# passes
    // THAT to Path.Combine again. Since it is already absolute, the second
    // Combine discards themeDir (a rooted second argument wins), so the copy
    // target is the plain `themeDir\filename`. Reproduced through the same
    // rule rather than by "fixing" the expression.
    const std::string file_name = path::get_file_name(file_path);
    const std::string copy_file = path::combine(theme_dir, file_name);

    fs::copy_file(fs::u8path(file_path), fs::u8path(copy_file),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("cannot copy theme file: " + file_path);

    models::ThemeModel theme;
    theme.name = name;
    theme.description = description;  // C# passes a plain string, so never null here
    theme.contact = std::nullopt;
    theme.license = std::nullopt;
    theme.accent_color = std::nullopt;
    theme.tags = std::nullopt;
    theme.file = copy_file;
    theme.preview = copy_file;
    theme.type = models::ThemeType::picture;
    theme.is_editable = true;

    // Persisted form: filenames only, relative to the theme directory — built via
    // the C# copy constructor, which (quirk) re-initializes AppVersion rather
    // than carrying it over.
    models::ThemeModel persisted = models::ThemeModel::copy_constructor(theme);
    persisted.file = path::get_file_name(*theme.file);
    persisted.preview = path::get_file_name(*theme.preview);
    JsonStorage::StoreThemeData(path::combine(theme_dir, "theme.json"), persisted);

    return theme;  // absolute-path model, as the C# returns
}

models::ThemeModel AppThemeFactory::CreateFromDirectory(const std::string& theme_dir) {
    const std::string metadata = path::combine(theme_dir, "theme.json");
    if (!file_exists(metadata)) {
        // C# throws FileNotFoundException; callers only branch on failure.
        throw std::runtime_error("theme metadata not found: " + metadata);
    }

    models::ThemeModel theme = JsonStorage::LoadThemeData(metadata);
    // Re-root the relative filenames at the directory they were found in — this
    // is what makes a moved theme directory still work.
    //
    // C# calls Path.Combine(themeDir, theme.File) with no null guard, and
    // Path.Combine(string, null) throws ArgumentNullException — so a theme.json
    // whose File member is absent fails here rather than resolving to the
    // directory itself. (Path.Combine(dir, "") would quietly give "dir\", which
    // is the trap a `value_or("")` port falls into.)
    if (!theme.file || !theme.preview) {
        throw std::runtime_error("theme metadata has no File/Preview: " + metadata);
    }
    // `new ThemeModel(theme) { File = ..., Preview = ..., IsEditable = true }` —
    // again through the copy constructor, so the returned model carries the
    // host's AppVersion rather than the value stored in the file.
    models::ThemeModel resolved = models::ThemeModel::copy_constructor(theme);
    resolved.file = path::combine(theme_dir, *theme.file);
    resolved.preview = path::combine(theme_dir, *theme.preview);
    resolved.is_editable = true;
    return resolved;
}

} // namespace lively::common::factories
