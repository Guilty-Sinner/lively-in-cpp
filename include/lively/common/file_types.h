#pragma once
// Port of Lively.Common/FileTypes.cs — the supported wallpaper file types and
// the "is this a Lively wallpaper package" test.
//
// C#:
//   SupportedFormats            — ordered table, "if more than one wallpapertype
//                                 has the same extension, first result is
//                                 selected" (.html is web *then* webaudio).
//   GetFileType(path)           — extension lookup, OrdinalIgnoreCase. -1 when
//                                 unsupported (the doc-comment's "100 if Lively
//                                 .zip" is stale; the oracle emits -1 for .zip).
//   IsWallpaperPackage(path)    — SharpZipLib ZipFile.FindEntry("LivelyInfo.json",
//                                 ignoreCase: true); any IO/format error → false.
//   IsWallpaperPackageExtension — OrdinalIgnoreCase ".zip" comparison.

#include <lively/models/library_model.h>
#include <lively/models/wallpaper_type.h>

#include <string>
#include <vector>

namespace lively::common {

// -1 == "not supported", matching the C# cast (WallpaperType)(-1).
inline constexpr int kUnsupportedFileType = -1;

const std::vector<models::FileTypeModel>& supported_formats();

int get_file_type(const std::string& file_path);

// True when the archive contains a root-level LivelyInfo.json entry (the entry
// name is compared case-insensitively, as SharpZipLib does with ignoreCase).
// A missing file, a non-zip file or a truncated archive all return false — the
// C# body swallows every exception.
bool is_wallpaper_package(const std::string& archive_path);

bool is_wallpaper_package_extension(const std::string& file_path);

} // namespace lively::common
