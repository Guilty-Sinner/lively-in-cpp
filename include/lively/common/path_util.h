#pragma once
// Ports of the System.IO.Path primitives the ported code depends on.
//
// These are the .NET semantics, not std::filesystem's: std::filesystem uses the
// platform's native separators and its own rules for roots, while the Lively
// code paths were written against Path.Combine/GetFileName/GetDirectoryName and
// the strings they produce end up inside livelyinfo.json — so the exact shapes
// matter. tests/test_library.cpp compares them against the C# oracle.

#include <optional>
#include <string>

namespace lively::common::path {

// Path.Combine: a rooted second argument wins; an empty first argument is
// ignored. Never throws on modern .NET, hence no error path.
std::string combine(const std::string& first, const std::string& second);

// Path.Combine wrapped in the C# TryPathCombine helper: a null (absent)
// component throws ArgumentNullException in .NET, which the caller catches and
// turns into null.
std::optional<std::string> combine_optional(const std::string& first,
                                            const std::optional<std::string>& second);

// "" when the name has no '.', ends with '.', or the dot is in a directory part.
std::string get_extension(const std::string& path);

// Path.HasExtension.
bool has_extension(const std::string& path);

std::string get_file_name(const std::string& path);

std::string get_file_name_without_extension(const std::string& path);

// Path.GetDirectoryName: nullopt when the path has no directory part (matching
// the C# null, which callers like LinkUtil.GetStableHostName coalesce).
std::optional<std::string> get_directory_name(const std::string& path);

} // namespace lively::common::path
