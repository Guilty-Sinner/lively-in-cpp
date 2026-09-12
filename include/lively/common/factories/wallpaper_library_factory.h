#pragma once
// Port of Lively.Common/Factories/WallpaperLibraryFactory.cs — turning a folder
// (or a piece of metadata) into a library entry.
//
// This is the headless core of the app: everything the UI shows about a
// wallpaper comes from LibraryModel, and every path in it is decided here. The
// oracle (tests/goldens/library_csharp.txt) pins the resolution rules for the
// four metadata shapes that matter — absolute local, relative local, online and
// media — including the two traps in the original:
//
//   * with IsAbsolutePath the property file is looked up next to the *executable*,
//     not in the wallpaper folder;
//   * media wallpapers fall back to the bundled player properties in
//     %LOCALAPPDATA%\Lively Wallpaper\Mpv whether or not those files exist.
//
// `CreateMediaWallpaperPackageAsync` needs a 512x512 thumbnail, which C# obtains
// through the shell (ThumbnailUtil → IShellItemImageFactory). The ported entry
// point therefore takes a thumbnail provider: pass nullptr and it reports
// "cannot be built" instead of silently producing metadata with no thumbnail.
//
// The C# *Async methods only move blocking file I/O onto the thread pool (and the
// await is immediately waited on by every core call-site), so the port keeps them
// synchronous and drops the suffix.

#include <lively/models/library_model.h>
#include <lively/models/wallpaper_type.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace lively::common {

// The factory's failure modes are BCL exception types upstream
// (FileNotFoundException / JsonReaderException / InvalidOperationException),
// which have no C++ counterpart. Named types keep the distinction the call-sites
// rely on instead of collapsing everything into one runtime_error; the oracle
// transcript maps each back to its C# name.
class WallpaperMetadataNotFoundException : public std::runtime_error {
public:
    explicit WallpaperMetadataNotFoundException(const std::string& message)
        : std::runtime_error(message) {}
};

class WallpaperMetadataCorruptedException : public std::runtime_error {
public:
    explicit WallpaperMetadataCorruptedException(const std::string& message)
        : std::runtime_error(message) {}
};

class WallpaperAlreadyPackagedException : public std::runtime_error {
public:
    explicit WallpaperAlreadyPackagedException(const std::string& message)
        : std::runtime_error(message) {}
};

class WallpaperLibraryFactory {
public:
    // Provider that writes a JPEG thumbnail of `source` to `dest`; returns false
    // when it could not produce one.
    using ThumbnailProvider =
        std::function<bool(const std::string& source, const std::string& dest)>;

    // Throws (FileNotFoundException / JsonReaderException in C#) when
    // LivelyInfo.json is absent or malformed.
    models::LivelyInfoModel get_metadata(const std::string& folder_path) const;

    models::LibraryModel create_from_directory(const std::string& folder_path) const;

    models::LibraryModel create_from_metadata(const models::LivelyInfoModel& metadata) const;

    // Writes `<dest_directory>/LivelyInfo.json`. `arguments` has no value by
    // default, matching the C# optional parameter.
    models::LivelyInfoModel create_wallpaper_package(
        const std::string& file_path, const std::string& dest_directory,
        models::WallpaperType type,
        const std::optional<std::string>& arguments = std::nullopt) const;

    std::optional<models::LivelyInfoModel> create_media_wallpaper_package(
        const std::string& file_path, const std::string& dest_directory, bool copy_file_to_dest,
        const ThumbnailProvider& thumbnail_provider) const;

    void convert_absolute_to_relative_path(models::LivelyInfoModel& metadata,
                                           const std::string& folder_path) const;
};

} // namespace lively::common
