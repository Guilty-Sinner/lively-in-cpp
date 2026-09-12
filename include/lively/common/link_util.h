#pragma once
// Port of Lively.Common/LinkUtil.cs.
//
// The C# code leans on System.Uri, which brings a large amount of parsing and
// normalization behaviour with it. The port reproduces the parts the app
// depends on and is explicit about the rest:
//
//   GetLastSegmentUrl  — the version used when importing a wallpaper ("the last
//                        path segment becomes the title"), including the
//                        "no segment → host, minus every 'www.'" rule and the
//                        "not a URI → return the input untouched" fallback.
//   GetStableHostName  — SHA-1 over the containing directory, first 8 bytes as
//                        lowercase hex (the CEF/WebView2 virtual-host name).
//   SanitizeUrl        — scheme/host lowercasing, empty-path → "/", default-port
//                        removal and the "assume https://" fallback.
//
// System.Uri also accepts UNC paths, IPv6 literals, IDN hosts and other forms
// this port does not model; those inputs are reported as invalid rather than
// guessed at.

#include <optional>
#include <string>

namespace lively::common {

// .NET `new Uri(address)` + `uri.Segments.Last()`; returns `url` unchanged when
// the input is not an absolute URI (the C# catch-return-url path).
std::string get_last_segment_url(const std::string& url);

// Stable, hostname-safe identifier for the directory containing `file_path`.
std::string get_stable_host_name(const std::string& file_path);

// Throws std::invalid_argument for empty/whitespace input and for text that
// cannot become a URI (the C# ArgumentException / UriFormatException paths).
std::string sanitize_url(const std::string& address);

std::optional<std::string> try_sanitize_url(const std::string& address);

// Shell-opens the address in the default browser; failures are swallowed.
void open_browser(const std::string& address);

} // namespace lively::common
