// Differential fuzz target: reads "case<TAB>ignored" or plain lines, runs
// lively::utility::ParseCommandLine, prints "case<TAB>outcome" using the same
// canonical format as the C# FuzzProbe (tools/csharp_probe/FuzzProbe.cs):
//   OK <verb>;opt=value;...
//   ERR <sorted errors>
#include "lively/utility/commandline.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace lively::utility;

namespace {

std::string JoinTags(std::vector<std::string> tags);

std::string JoinOpts(const std::vector<std::pair<std::string, std::string>>& opts) {
    std::string out;
    for (const auto& [k, v] : opts) {
        out += ";" + k + "=" + v;
    }
    return out;
}

std::string BoolStr(bool v) { return v ? "true" : "false"; }

std::string Describe(const ParseResult& r) {
    if (!r.success) {
        // Canonicalize our error strings to the CommandLineParser tags the C#
        // oracle emits (sorted, comma-joined). We produce the same tags at the
        // same sites: unknown option -> UnknownOptionsError:token, missing
        // value -> MissingValueOptionError, missing required -> MissingRequiredOptionError.
        std::vector<std::string> tags;
        for (const auto& e : r.errors) {
            if (e.rfind("unknown option: ", 0) == 0) {
                tags.push_back("UnknownOptionsError:" + e.substr(16));
            } else if (e.rfind("unknown verb: ", 0) == 0) {
                tags.push_back("UnknownVerbError:" + e.substr(14));
            } else if (e.rfind("missing value for --", 0) == 0) {
                tags.push_back("MissingValueOptionError");
            } else if (e.rfind("required option missing: --", 0) == 0) {
                tags.push_back("MissingRequiredOptionError:" + e.substr(27));
            } else if (e.rfind("bad bool for --", 0) == 0 || e.rfind("bad int for --", 0) == 0) {
                tags.push_back("FormatError");
            } else if (e.rfind("unexpected token: ", 0) == 0) {
                tags.push_back("UnknownOptionsError:" + e.substr(18));
            } else {
                tags.push_back(e); // pass-through (visible divergence to fix)
            }
        }
        return "ERR " + JoinTags(tags);
    }

    std::string opts;
    if (const auto* o = std::get_if<AppOptions>(&r.options)) {
        opts = "OK app";
        if (o->show_app) opts += ";showApp=" + BoolStr(*o->show_app);
        if (o->show_icons) opts += ";showIcons=" + BoolStr(*o->show_icons);
        if (o->volume) opts += ";volume=" + *o->volume;
        if (o->play) opts += ";play=" + BoolStr(*o->play);
        if (o->startup) opts += ";startup=" + BoolStr(*o->startup);
        if (o->shutdown_app) opts += ";shutdown=" + BoolStr(*o->shutdown_app);
        if (o->restart_app) opts += ";restart=" + BoolStr(*o->restart_app);
        if (o->wallpaper_arrangement) opts += ";layout=" + *o->wallpaper_arrangement;
        return opts;
    }
    if (const auto* o = std::get_if<SetWallpaperOptions>(&r.options)) {
        return "OK setwp;file=" + o->file +
               (o->monitor ? ";monitor=" + std::to_string(*o->monitor) : "");
    }
    if (const auto* o = std::get_if<CloseWallpaperOptions>(&r.options)) {
        return "OK closewp;monitor=" + std::to_string(o->monitor);
    }
    if (const auto* o = std::get_if<SeekWallpaperOptions>(&r.options)) {
        return "OK seekwp;value=" + o->param +
               (o->monitor ? ";monitor=" + std::to_string(*o->monitor) : "");
    }
    if (const auto* o = std::get_if<CustomiseWallpaperOptions>(&r.options)) {
        return "OK setprop;property=" + o->param +
               (o->monitor ? ";monitor=" + std::to_string(*o->monitor) : "");
    }
    if (const auto* o = std::get_if<ScreenSaverOptions>(&r.options)) {
        opts = "OK screensaver";
        if (o->preview) opts += ";preview=" + std::to_string(*o->preview);
        if (o->configure) opts += ";configure=" + BoolStr(*o->configure);
        if (o->show) opts += ";show=" + BoolStr(*o->show);
        if (o->show_exclusive) opts += ";showExclusive=" + BoolStr(*o->show_exclusive);
        if (o->is_fade_in) opts += ";fadeIn=" + BoolStr(*o->is_fade_in);
        return opts;
    }
    if (const auto* o = std::get_if<ScreenshotOptions>(&r.options)) {
        return "OK screenshot;file=" + o->file +
               (o->monitor ? ";monitor=" + std::to_string(*o->monitor) : "");
    }
    return "ERR unknown-type";
}

std::string JoinTags(std::vector<std::string> tags) {
    // CommandLineParser emits errors in tag order; sort to canonicalize.
    std::sort(tags.begin(), tags.end());
    std::string out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i) out += ",";
        out += tags[i];
    }
    return out;
}

} // namespace
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: lively_fuzz_target <cases-file>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::cerr << "cannot open: " << argv[1] << "\n";
        return 2;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> argv_vec;
        if (!line.empty()) {
            std::istringstream iss(line);
            std::string tok;
            while (iss >> tok) argv_vec.push_back(tok);
        }
        std::cout << line << "\t" << Describe(ParseCommandLine(argv_vec)) << "\n";
    }
    return 0;
}
