#pragma once
// Port of Lively.Common/CommandlineArgs.cs — the `livelycmd` automation verbs.
//
// Reference C# uses CommandLineParser (CommandLine.Utils) with verbs:
//   app (default) | setwp | closewp | seekwp | setprop | screensaver | screenshot
//
// This port implements the option model 1:1 (names, required-ness, types) with a
// small parser. Behaviour matched:
//   * "--name value" and "--name=value" forms
//   * required options missing -> parse error (C# prints usage; handled in main)
//   * unknown verb/option -> parse error
//   * nullable bool/int/string fields stay optional
//   * SetWallpaperOptions.IsRandom / IsReload use OrdinalIgnoreCase on File

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lively::utility {

// --- CommandLineParser option model -----------------------------------------
//
// Lively drives CommandLineParser 2.9.1 from two places: the `livelycmd` verbs
// (CommandlineArgs.cs) and the per-player `StartArgs` classes. Both go through
// the identical library pipeline, so the option model lives here and is shared
// (see lively/players/start_args.h).

enum class OptionKind {
    boolean,         // bool?  — TargetType.Scalar: needs a value ("--x true")
    boolean_switch,  // bool   — TargetType.Switch: presence alone means true and
                     //          any attached value is IGNORED (so "--x false"
                     //          and "--x=false" both still yield true — this is
                     //          the real CommandLineParser 2.9.1 behaviour,
                     //          oracle-verified, and a classic porting trap)
    integer,         // int / int?
    number,          // double / double? (Convert.ChangeType, invariant culture)
    text,            // string
    enumeration,     // enum — member name (case-SENSITIVE, like Enum.Parse's
                     //          default) or a *defined* member ordinal
};

struct OptionSpec {
    const char* name;
    OptionKind kind;
    bool required;
    // enumeration only: accepted member names, in declaration order. A numeric
    // token is accepted as-is (C# Enum.Parse does not range-check).
    const char* const* enum_names = nullptr;
    std::size_t enum_count = 0;
};

// Outcome of a verbless parse (one [Option]-annotated class).
struct OptionValues {
    bool success = false;
    // Last successfully bound value per option. For `enumeration` this is
    // normalised to the member ordinal (numeric input passes through).
    std::map<std::string, std::string> values;
    std::string error;  // canonical tag list, same shape as ParseCommandLine
};

// Parses args against an option set, matching CommandLineParser 2.9.1
// (tokenizer -> scalar pairing -> conversion -> repeated -> required).
OptionValues ParseOptions(const OptionSpec* options, std::size_t count,
                          const std::vector<std::string>& args);

// --- verb option records (field names match C# properties) ---

struct AppOptions {
    std::optional<bool> show_app;
    std::optional<bool> show_icons;
    std::optional<std::string> volume;
    std::optional<bool> play;
    std::optional<bool> startup;
    std::optional<bool> shutdown_app;
    std::optional<bool> restart_app;
    std::optional<std::string> wallpaper_arrangement;
};

struct SetWallpaperOptions {
    std::string file;                 // Required
    std::optional<int> monitor;
    // C#: string.Equals(File, "random", StringComparison.OrdinalIgnoreCase)
    bool equals_ignore_case(const char* other) const noexcept {
        if (file.size() != std::char_traits<char>::length(other)) return false;
        for (std::size_t i = 0; i < file.size(); ++i) {
            const char a = file[i], b = other[i];
            const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
            const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
            if (la != lb) return false;
        }
        return true;
    }
    bool is_random() const noexcept { return equals_ignore_case("random"); }
    bool is_reload() const noexcept { return equals_ignore_case("reload"); }
};

struct CloseWallpaperOptions {
    int monitor;                      // Required
};

struct SeekWallpaperOptions {
    std::string param;                // Required ("--value")
    std::optional<int> monitor;
};

struct CustomiseWallpaperOptions {
    std::string param;                // Required ("--property")
    std::optional<int> monitor;
};

struct ScreenSaverOptions {
    std::optional<int> preview;
    std::optional<bool> configure;
    std::optional<bool> show;
    std::optional<bool> show_exclusive;
    std::optional<bool> is_fade_in;
};

struct ScreenshotOptions {
    std::string file;                 // Required
    std::optional<int> monitor;
};

using ParsedOptions = std::variant<
    AppOptions, SetWallpaperOptions, CloseWallpaperOptions, SeekWallpaperOptions,
    CustomiseWallpaperOptions, ScreenSaverOptions, ScreenshotOptions>;

struct ParseResult {
    bool success = false;
    ParsedOptions options{AppOptions{}};
    std::vector<std::string> errors;  // CommandLineParser Error equivalents
};

// Parses argv (program name excluded), matching the C# verb mapping.
ParseResult ParseCommandLine(const std::vector<std::string>& args);

} // namespace lively::utility
