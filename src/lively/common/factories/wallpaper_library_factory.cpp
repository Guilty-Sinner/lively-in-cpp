#include <lively/common/factories/wallpaper_library_factory.h>

#include <lively/common/constants.h>
#include <lively/common/file_types.h>
#include <lively/common/file_util.h>
#include <lively/common/json_util.h>
#include <lively/common/link_util.h>
#include <lively/common/path_util.h>
#include <lively/common/wallpaper_extensions.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace lively::common {

namespace fs = std::filesystem;
using models::LivelyInfoModel;
using models::LibraryModel;
using models::WallpaperType;

namespace {

constexpr const char* kLivelyInfoFile = "LivelyInfo.json";
constexpr const char* kLivelyInfoLocFile = "LivelyInfo.loc.json";
constexpr const char* kLivelyPropertiesFile = "LivelyProperties.json";
constexpr const char* kLivelyPropertiesLocFile = "LivelyProperties.loc.json";

// Narrow view of Constants.CommonPaths.TempVideoDir (the bundled player's
// default properties folder), matching the C# string API used here.
std::string TempVideoDir() { return TempVideoDirNarrow(); }

bool file_exists(const std::optional<std::string>& path) {
    // C# File.Exists(null) is false.
    if (!path.has_value()) return false;
    std::error_code ec;
    return fs::is_regular_file(*path, ec);
}

// Path.GetRandomFileName(): only ever used to name a generated thumbnail, so a
// time-seeded name with the same 8.3 shape is enough (nothing reads it back).
std::string random_file_name() {
    static std::atomic<unsigned> counter{0};
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const auto seed = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()) +
                      (++counter);
    std::mt19937_64 rng(seed);
    std::string name(8, 'a');
    for (char& c : name) c = alphabet[rng() % (sizeof(alphabet) - 1)];
    return name;
}

} // namespace

LivelyInfoModel WallpaperLibraryFactory::get_metadata(const std::string& folder_path) const {
    const std::string metadata_path = path::combine(folder_path, kLivelyInfoFile);
    std::error_code ec;
    if (!fs::is_regular_file(metadata_path, ec)) {
        throw WallpaperMetadataNotFoundException("LivelyInfo.json not found");
    }
    // JsonStorage<LivelyInfoModel>.LoadData: malformed JSON throws.
    try {
        return JsonStorage::LoadLivelyInfoData(metadata_path);
    } catch (const std::exception&) {
        throw WallpaperMetadataCorruptedException("Corrupted wallpaper metadata");
    }
}

LibraryModel WallpaperLibraryFactory::create_from_directory(const std::string& folder_path) const {
    // JsonStorage<LivelyInfoModel>.LoadData also throws ArgumentNullException on
    // a JSON `null` document, hence the extra guard.
    const LivelyInfoModel metadata = get_metadata(folder_path);

    LibraryModel result;
    result.lively_info = metadata;
    result.lively_info_folder_path = folder_path;
    result.lively_info_localization_path = path::combine(folder_path, kLivelyInfoLocFile);
    result.lively_property_localization_path = path::combine(folder_path, kLivelyPropertiesLocFile);
    result.is_subscribed = !metadata.id.value_or("").empty();
    result.set_title(metadata.title);
    result.set_desc(metadata.desc);
    result.set_author(metadata.author);

    if (metadata.is_absolute_path) {
        // The full file path lives in the metadata file.
        result.file_path = metadata.file_name;
        // Backward compatibility with older wallpaper files: preview and thumb
        // always live inside the folder even when the file path is absolute.
        // Path.GetFileName(null) throws in C#, and nothing catches it here — so
        // absent Preview/Thumbnail is a hard failure, as upstream.
        if (!metadata.preview.has_value()) {
            throw std::invalid_argument("Path.GetFileName: Preview is null");
        }
        if (!metadata.thumbnail.has_value()) {
            throw std::invalid_argument("Path.GetFileName: Thumbnail is null");
        }
        result.preview_clip_path =
            path::combine_optional(folder_path, path::get_file_name(*metadata.preview));
        result.thumbnail_path =
            path::combine_optional(folder_path, path::get_file_name(*metadata.thumbnail));

        const auto parent = path::get_directory_name(metadata.file_name.value_or(""));
        result.lively_property_path =
            parent.has_value() ? std::optional<std::string>(
                                     path::combine(*parent, kLivelyPropertiesFile))
                               : std::nullopt;
    } else {
        // Only a relative path is stored, so the wallpaper lives in appdata.
        if (is_online_wallpaper(metadata.type)) {
            result.file_path = metadata.file_name;
        } else {
            result.file_path = path::combine_optional(folder_path, metadata.file_name);
            result.lively_property_path =
                path::combine_optional(folder_path, kLivelyPropertiesFile);
        }
        result.preview_clip_path = path::combine_optional(folder_path, metadata.preview);
        result.thumbnail_path = path::combine_optional(folder_path, metadata.thumbnail);
    }

    // Verify, then prefer the preview clip for the tile image.
    if (!file_exists(result.thumbnail_path)) result.thumbnail_path = std::nullopt;
    if (!file_exists(result.preview_clip_path)) result.preview_clip_path = std::nullopt;
    result.image_path = result.preview_clip_path.has_value() ? result.preview_clip_path
                                                            : result.thumbnail_path;

    // Default video player property, otherwise verify customisability.
    if (is_media_wallpaper(metadata.type)) {
        if (!file_exists(result.lively_property_path)) {
            result.lively_property_path = path::combine(TempVideoDir(), kLivelyPropertiesFile);
        }
        if (!file_exists(result.lively_property_localization_path)) {
            result.lively_property_localization_path =
                path::combine(TempVideoDir(), kLivelyPropertiesLocFile);
        }
    } else if (!file_exists(result.lively_property_path)) {
        result.lively_property_path = std::nullopt;
    }

    return result;
}

LibraryModel WallpaperLibraryFactory::create_from_metadata(const LivelyInfoModel& metadata) const {
    LibraryModel result;
    result.lively_info = metadata;
    result.set_title(metadata.title);
    result.set_desc(metadata.desc);
    result.set_author(metadata.author);
    result.image_path = file_exists(metadata.preview) ? metadata.preview : metadata.thumbnail;
    return result;
}

LivelyInfoModel WallpaperLibraryFactory::create_wallpaper_package(
    const std::string& file_path, const std::string& dest_directory, WallpaperType type,
    const std::optional<std::string>& arguments) const {
    if (is_directory_project(type)) {
        const std::string folder = path::get_directory_name(file_path).value_or("");
        const std::string metadata_path = path::combine(folder, kLivelyInfoFile);
        std::error_code ec;
        if (fs::exists(metadata_path, ec)) {
            throw WallpaperAlreadyPackagedException(
                "A LivelyInfo.json file was found in this project folder.\n"
                "It looks like this project is already packaged.\n"
                "To create a new project, please remove the existing LivelyInfo.json first.");
        }
    }

    LivelyInfoModel metadata;
    metadata.title = is_online_wallpaper(type) ? get_last_segment_url(file_path)
                                               : path::get_file_name_without_extension(file_path);
    metadata.type = type;
    metadata.is_absolute_path = true;
    metadata.file_name = file_path;
    metadata.contact = is_online_wallpaper(type) ? file_path : std::string();
    metadata.preview = std::string();
    metadata.thumbnail = std::string();
    metadata.arguments = arguments;

    fs::create_directories(dest_directory);
    JsonStorage::StoreLivelyInfoData(path::combine(dest_directory, kLivelyInfoFile), metadata);
    return metadata;
}

std::optional<LivelyInfoModel> WallpaperLibraryFactory::create_media_wallpaper_package(
    const std::string& file_path, const std::string& dest_directory, bool copy_file_to_dest,
    const ThumbnailProvider& thumbnail_provider) const {
    const int file_type = get_file_type(file_path);
    if (file_type == kUnsupportedFileType) return std::nullopt;
    const auto type = static_cast<WallpaperType>(file_type);
    if (!is_media_wallpaper(type) || is_online_wallpaper(type)) return std::nullopt;
    if (!thumbnail_provider) return std::nullopt; // see the header: shell thumbnail not ported

    LivelyInfoModel metadata;
    metadata.title = path::get_file_name_without_extension(file_path);
    metadata.type = type;
    metadata.is_absolute_path = true;
    metadata.file_name = file_path;
    metadata.contact = std::string();
    metadata.preview = std::string();
    metadata.thumbnail = path::combine(dest_directory, random_file_name() + ".jpg");
    metadata.arguments = std::string();

    fs::create_directories(dest_directory);
    if (!thumbnail_provider(file_path, *metadata.thumbnail)) return std::nullopt;

    JsonStorage::StoreLivelyInfoData(path::combine(dest_directory, kLivelyInfoFile), metadata);
    if (copy_file_to_dest) {
        convert_absolute_to_relative_path(metadata, dest_directory);
    }
    return metadata;
}

void WallpaperLibraryFactory::convert_absolute_to_relative_path(LivelyInfoModel& metadata,
                                                               const std::string& folder_path) const {
    if (!metadata.is_absolute_path) return;

    switch (metadata.type) {
    case WallpaperType::video:
    case WallpaperType::gif:
    case WallpaperType::picture: {
        const std::string source = metadata.file_name.value_or("");
        const std::string destination = path::combine(folder_path, path::get_file_name(source));
        fs::copy_file(source, destination); // File.Copy(..., overwrite: false)
        metadata.file_name = path::get_file_name(source);
        break;
    }
    case WallpaperType::unityaudio:
    case WallpaperType::app:
    case WallpaperType::web:
    case WallpaperType::webaudio:
    case WallpaperType::bizhawk:
    case WallpaperType::unity:
    case WallpaperType::godot: {
        const std::string source_dir = path::get_directory_name(metadata.file_name.value_or("")).value_or("");
        directory_copy(source_dir, folder_path, true);
        metadata.file_name = path::get_file_name(metadata.file_name.value_or(""));
        break;
    }
    case WallpaperType::url:
    case WallpaperType::videostream:
        // Nothing to do.
        break;
    }

    metadata.thumbnail = file_exists(metadata.thumbnail)
                             ? std::optional<std::string>(path::get_file_name(*metadata.thumbnail))
                             : std::nullopt;
    metadata.preview = file_exists(metadata.preview)
                           ? std::optional<std::string>(path::get_file_name(*metadata.preview))
                           : std::nullopt;
    metadata.is_absolute_path = false;

    JsonStorage::StoreLivelyInfoData(path::combine(folder_path, kLivelyInfoFile), metadata);
}

} // namespace lively::common
