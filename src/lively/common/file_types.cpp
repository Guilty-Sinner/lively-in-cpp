#include <lively/common/file_types.h>

#include <lively/common/path_util.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace lively::common {

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::uint16_t read_u16(const std::vector<char>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset]) |
                                      (static_cast<unsigned char>(data[offset + 1]) << 8));
}

std::uint32_t read_u32(const std::vector<char>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

std::uint64_t read_u64(const std::vector<char>& data, std::size_t offset) {
    return static_cast<std::uint64_t>(read_u32(data, offset)) |
           (static_cast<std::uint64_t>(read_u32(data, offset + 4)) << 32);
}

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50;
constexpr std::uint32_t kZip64Locator = 0x07064b50;
constexpr std::uint32_t kZip64EndOfCentralDir = 0x06064b50;
constexpr std::uint32_t kCentralFileHeader = 0x02014b50;

// Minimal zip central-directory reader. Lively only ever asks "does this archive
// contain LivelyInfo.json?", so the port walks the central directory rather than
// depending on a zip library (the C# side uses SharpZipLib).
std::optional<std::string> find_zip_entry(const std::vector<char>& data, const std::string& wanted) {
    const std::string target = to_lower(wanted);

    if (data.size() < 22) return std::nullopt;
    // The EOCD record may be followed by a comment of up to 64 KiB.
    const std::size_t max_back = std::min<std::size_t>(data.size(), 22 + 0xFFFF);
    const std::size_t lowest = data.size() - max_back;
    std::size_t eocd = std::string::npos;
    for (std::size_t i = data.size() - 22;; --i) {
        if (read_u32(data, i) == kEndOfCentralDir) {
            eocd = i;
            break;
        }
        if (i <= lowest) break;
    }
    if (eocd == std::string::npos) return std::nullopt;

    std::uint64_t entries = read_u16(data, eocd + 10);
    std::uint64_t offset = read_u32(data, eocd + 16);

    // Zip64: the 32-bit fields saturate, so follow the locator.
    if (entries == 0xFFFF || offset == 0xFFFFFFFF) {
        std::size_t locator = std::string::npos;
        for (std::size_t i = eocd >= 20 ? eocd - 20 : 0; i + 4 <= eocd; ++i) {
            if (read_u32(data, i) == kZip64Locator) {
                locator = i;
                break;
            }
        }
        if (locator == std::string::npos) return std::nullopt;
        const std::uint64_t zip64_eocd = read_u64(data, locator + 8);
        if (zip64_eocd + 56 > data.size() || read_u32(data, static_cast<std::size_t>(zip64_eocd)) !=
                                                 kZip64EndOfCentralDir) {
            return std::nullopt;
        }
        entries = read_u64(data, static_cast<std::size_t>(zip64_eocd) + 32);
        offset = read_u64(data, static_cast<std::size_t>(zip64_eocd) + 48);
    }

    std::size_t cursor = static_cast<std::size_t>(offset);
    for (std::uint64_t i = 0; i < entries; ++i) {
        if (cursor + 46 > data.size() || read_u32(data, cursor) != kCentralFileHeader) {
            return std::nullopt;
        }
        const std::uint16_t name_len = read_u16(data, cursor + 28);
        const std::uint16_t extra_len = read_u16(data, cursor + 30);
        const std::uint16_t comment_len = read_u16(data, cursor + 32);
        if (cursor + 46 + name_len > data.size()) return std::nullopt;
        const std::string name(data.data() + cursor + 46, name_len);
        if (to_lower(name) == target) return name;
        cursor += 46u + name_len + extra_len + comment_len;
    }
    return std::nullopt;
}

} // namespace

const std::vector<models::FileTypeModel>& supported_formats() {
    using models::WallpaperType;
    static const std::vector<models::FileTypeModel> formats = {
        {WallpaperType::video,
         {".wmv", ".avi", ".flv", ".m4v", ".mkv", ".mov", ".mp4", ".mp4v", ".mpeg4",
          ".mpg", ".webm", ".ogm", ".ogv", ".ogx"}},
        {WallpaperType::picture,
         {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp", ".jfif"}},
        {WallpaperType::gif, {".gif"}},
        // WallpaperType.heic is commented out upstream.
        {WallpaperType::web, {".html"}},
        {WallpaperType::webaudio, {".html"}},
        {WallpaperType::app, {".exe"}},
        {WallpaperType::godot, {".exe"}},
    };
    return formats;
}

int get_file_type(const std::string& file_path) {
    const std::string ext = to_lower(path::get_extension(file_path));
    if (ext.empty()) return kUnsupportedFileType;
    for (const auto& format : supported_formats()) {
        for (const auto& candidate : format.extensions) {
            if (to_lower(candidate) == ext) return static_cast<int>(format.type);
        }
    }
    return kUnsupportedFileType;
}

bool is_wallpaper_package(const std::string& archive_path) {
    std::ifstream in(archive_path, std::ios::binary);
    if (!in) return false;
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) return false;
    return find_zip_entry(data, "LivelyInfo.json").has_value();
}

bool is_wallpaper_package_extension(const std::string& file_path) {
    return to_lower(path::get_extension(file_path)) == ".zip";
}

} // namespace lively::common
