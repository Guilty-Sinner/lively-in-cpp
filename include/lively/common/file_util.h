#pragma once
// Port of Lively.Common/Helpers/Files/FileUtil.cs.
//
// Kept as free functions in lively::common (C# static class); the deterministic
// helpers are pinned byte-for-byte by the oracle in tests/goldens/library_csharp.txt.
//
// Deliberate deviations, both documented at their definitions:
//   * SizeSuffix formats with the invariant culture. C# uses CurrentCulture, so
//     a de-DE user sees "1,5 KB"; the only consumers are a log line and a tile
//     subtitle, and the oracle pins invariant so the value is comparable.
//   * GetFiles' final `List.Sort()` is ordinal here (C# uses the current-culture
//     comparer). Filesystem names are not a place culture-aware collation should
//     decide behaviour, and ordinal matches the oracle for ASCII names.

#include <cstdint>
#include <string>
#include <vector>

namespace lively::common {

// Opens the containing folder in Explorer, selecting the file when given a file
// path. Errors are swallowed (!), exactly like the C# try/catch.
void open_folder(const std::string& path);

// Replaces invalid filename characters with '_' (Path.GetInvalidFileNameChars
// + split/join, so runs of invalid characters produce runs of underscores).
std::string get_safe_filename(const std::string& filename);

// "name.ext" -> "name (1).ext", picking the first free index. Reproduces the
// exponential-probe + binary-search shape of the C# implementation, including
// the " (n)" pattern placement before the extension.
std::string next_available_filename(const std::string& path);

// Lowercase hex SHA-256 of the file's contents (BCrypt on Windows).
std::string get_checksum_sha256(const std::string& file_path);

// Deletes the contents of a directory (files, then subdirectories recursively)
// but keeps the directory itself.
void empty_directory(const std::string& directory);

// '|'-separated search patterns; results are concatenated per pattern and sorted
// ascending, matching C# FileUtil.GetFiles.
std::vector<std::string> get_files(const std::string& path, const std::string& search_pattern,
                                   bool recursive);

// Size comparison that reports false instead of throwing when the file is
// missing or unreadable.
bool is_file_greater(const std::string& file_path, std::int64_t bytes);

// Recursive directory copy; throws when the source does not exist (mirrors the
// C# DirectoryNotFoundException).
void directory_copy(const std::string& source_dir, const std::string& dest_dir, bool copy_sub_dirs);

std::int64_t get_directory_size(const std::string& path);

// "1.5 KB"-style formatting. decimal_places < 0 throws (C# ArgumentOutOfRange).
std::string size_suffix(std::int64_t value, int decimal_places = 1);

// Async folder-delete with retry, made synchronous: sleeps before deleting and
// once more before the single retry, then reports success. Delays are in
// milliseconds (C# Task.Delay is too).
bool try_delete_directory(const std::string& folder_path, int initial_delay_ms, int retry_delay_ms);

} // namespace lively::common
