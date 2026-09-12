#pragma once
// Port of Lively.Common/WindowClassExclusions.cs — window classes that must not
// be treated as "a window the wallpaper should hide behind" (desktop, shell
// chrome, overlays).
//
// C# builds a HashSet<string> with StringComparer.OrdinalIgnoreCase, so lookups
// are case-insensitive and enumeration order is unspecified. The port keeps a
// vector plus an explicit case-insensitive contains() — callers iterate it and
// membership is the only operation that matters.

#include <string>
#include <vector>

namespace lively::common {

const std::vector<std::string>& desktop_classes();

// OrdinalIgnoreCase membership test (ASCII folding, matching .NET's
// case-insensitive comparer for the ASCII class names used here).
bool is_desktop_class(const std::string& window_class);

} // namespace lively::common
