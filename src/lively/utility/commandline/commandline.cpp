// Port of Lively.Common/CommandlineArgs.cs parsing — a faithful re-implementation
// of the CommandLineParser 2.9.1 pipeline (the exact version Lively references).
//
// Algorithm mirrors the real library sources (commandlineparser/commandline v2.9.1):
//   Tokenizer.Tokenize          — arg -> Name/Value tokens; "-<digit>" and lone
//                                 "-" are Values; "-x" is Name"x"; long names
//                                 "--n=v" split on first '=' (BadFormatTokenError
//                                 on "--=" / "--a="), "--a=b=c" -> n + "b=c";
//                                 "--" vanishes (EnableDashDash=false default);
//                                 unknown names are removed -> UnknownOptionError
//                                 (their value tokens remain in the stream).
//   PreprocessorGuards          — "--help" / "--version" as the first post-verb
//                                 argument fail the parse (HelpRequestedError /
//                                 VersionRequestedError).
//   TokenPartitioner            — ALL Lively options are TargetType.Scalar
//                                 (bool?/int?/string all map to Scalar — this is
//                                 why "--preview --configure" makes preview pair
//                                 with the NAME "configure"). Scalar name starts
//                                 a pair; the next Value completes it; a second
//                                 name just joins the scalar stream. Values with
//                                 no pending pair are loose (ignored — no Value
//                                 specs in Lively's verbs).
//   KeyValuePairHelper.ForScalar— the scalar token list is chunked into pairs of
//                                 two (Group(2)); a trailing incomplete group is
//                                 silently dropped. A pair's KEY may be a value
//                                 token (it then matches no option).
//   OptionMapper.MapValues      — per option: collect values from all matching
//                                 pairs (in stream order); conversion uses the
//                                 LAST value (int: invariant + Int32 range;
//                                 bool: "true"/"false" OrdinalIgnoreCase; string:
//                                 raw). Failure -> BadFormatConversionError and
//                                 the option stays unbound.
//   SpecificationPropertyRules  — EnforceSingle counts NAME tokens per option but
//                                 only for options that SUCCESSFULLY bound (sp.
//                                 Value.IsJust()) -> RepeatedOptionError; an
//                                 unbound option therefore never repeats.
//                                 EnforceRequired -> MissingRequiredOptionError.
//   Error order                 — tokenizer (BadFormatToken in-arg order, then
//                                 UnknownOption in token order), conversion
//                                 (declaration order), repeated (first-appearance
//                                 order), required (declaration order).
//   InstanceChooser             — "app" is the default verb; an unknown first
//                                 token falls through to the default verb with
//                                 the FULL argv; empty argv -> default verb OK.
//
// Verified against the real binaries via tools/csharp_probe "fuzz" mode +
// tools/fuzz_differential.py (differential fuzzer).

#include "lively/utility/commandline.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lively::utility {

namespace {

bool parse_bool_strict(const std::string& s, bool& out) {
    // StringExtensions.IsBooleanString/ToBoolean (OrdinalIgnoreCase).
    const auto eq = [&](const char* lit) {
        const std::size_t n = std::char_traits<char>::length(lit);
        if (s.size() != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            const char a = s[i], b = lit[i];
            const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
            const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
            if (la != lb) return false;
        }
        return true;
    };
    if (eq("true")) { out = true; return true; }
    if (eq("false")) { out = false; return true; }
    return false;
}

// Invariant int parse with Int32 range (Convert.ChangeType behaviour).
bool parse_int_strict(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        i = 1;
        if (s.size() == 1) return false;
    }
    long long value = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + (s[i] - '0');
        if (value > 2147483648LL) return false;
    }
    if (!negative && value > 2147483647LL) return false;
    out = negative ? static_cast<int>(-value) : static_cast<int>(value);
    return true;
}

// Invariant double parse (Convert.ChangeType -> double.Parse with
// NumberStyles.Float | AllowThousands): optional surrounding whitespace, sign,
// digits with an optional '.' fraction, optional exponent, ',' accepted as a
// (unvalidated) group separator in the integer part, and the special literals
// NaN / Infinity / -Infinity. Anything else fails.
bool parse_double_strict(const std::string& input, double& out) {
    const auto eq_ci = [](const std::string& s, const char* lit) {
        const std::size_t n = std::char_traits<char>::length(lit);
        if (s.size() != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            const char a = s[i], b = lit[i];
            const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
            const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
            if (la != lb) return false;
        }
        return true;
    };

    std::size_t begin = 0, end = input.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    if (begin == end) return false;
    std::string s = input.substr(begin, end - begin);

    if (eq_ci(s, "NaN")) { out = std::numeric_limits<double>::quiet_NaN(); return true; }
    if (eq_ci(s, "Infinity") || eq_ci(s, "+Infinity")) {
        out = std::numeric_limits<double>::infinity();
        return true;
    }
    if (eq_ci(s, "-Infinity")) { out = -std::numeric_limits<double>::infinity(); return true; }

    std::size_t i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    bool any_digit = false;
    for (; i < s.size(); ++i) {  // integer part (commas ignored as group separators)
        if (s[i] == ',') continue;
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) break;
        any_digit = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) break;
            any_digit = true;
        }
    }
    if (!any_digit) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        const std::size_t exp_start = i;
        for (; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) break;
        }
        if (i == exp_start) return false;
    }
    if (i != s.size()) return false;

    std::string cleaned;
    cleaned.reserve(s.size());
    for (const char c : s) {
        if (c != ',') cleaned.push_back(c);
    }
    try {
        std::size_t consumed = 0;
        out = std::stod(cleaned, &consumed);
        if (consumed != cleaned.size()) return false;
    } catch (...) { return false; }
    return true;
}

// Verb tables — [Option("name")] attributes from CommandlineArgs.cs.
constexpr OptionSpec kApp[] = {
    {"showApp", OptionKind::boolean, false},  {"showIcons", OptionKind::boolean, false},
    {"volume", OptionKind::text, false},      {"play", OptionKind::boolean, false},
    {"startup", OptionKind::boolean, false},  {"shutdown", OptionKind::boolean, false},
    {"restart", OptionKind::boolean, false},  {"layout", OptionKind::text, false},
};
constexpr OptionSpec kSetWp[] = {
    {"file", OptionKind::text, true}, {"monitor", OptionKind::integer, false},
};
constexpr OptionSpec kCloseWp[] = {
    {"monitor", OptionKind::integer, true},
};
constexpr OptionSpec kSeekWp[] = {
    {"value", OptionKind::text, true}, {"monitor", OptionKind::integer, false},
};
constexpr OptionSpec kSetProp[] = {
    {"property", OptionKind::text, true}, {"monitor", OptionKind::integer, false},
};
constexpr OptionSpec kScreensaver[] = {
    {"preview", OptionKind::integer, false}, {"configure", OptionKind::boolean, false},
    {"show", OptionKind::boolean, false},    {"showExclusive", OptionKind::boolean, false},
    {"fadeIn", OptionKind::boolean, false},
};
constexpr OptionSpec kScreenshot[] = {
    {"file", OptionKind::text, true}, {"monitor", OptionKind::integer, false},
};

struct OptionSet {
    const char* name;
    const OptionSpec* options;
    std::size_t count;
};

constexpr OptionSet kModels[] = {
    {"app", kApp, std::size(kApp)},
    {"setwp", kSetWp, std::size(kSetWp)},
    {"closewp", kCloseWp, std::size(kCloseWp)},
    {"seekwp", kSeekWp, std::size(kSeekWp)},
    {"setprop", kSetProp, std::size(kSetProp)},
    {"screensaver", kScreensaver, std::size(kScreensaver)},
    {"screenshot", kScreenshot, std::size(kScreenshot)},
};

const OptionSet* FindModel(const std::string& verb) {
    for (const auto& m : kModels) {
        if (verb == m.name) return &m;
    }
    return nullptr;
}

const OptionSpec* FindOption(const OptionSet& model, const std::string& name) {
    for (std::size_t i = 0; i < model.count; ++i) {
        if (name == model.options[i].name) return &model.options[i];
    }
    return nullptr;
}

enum class TokKind { kName, kValue };

struct Tok {
    TokKind kind;
    std::string text;
};

// Error tags in the library's emission order.
enum class Tag {
    kBadFormatToken,        // tokenizer (in-arg)
    kUnknownOption,         // tokenizer post-pass
    kBadFormatConversion,   // OptionMapper (declaration order)
    kMissingRequired,       // EnforceRequired
    kRepeatedOption,        // EnforceSingle
    kHelpRequested,         // PreprocessorGuards
    kVersionRequested,
};

struct PErr {
    Tag tag;
    std::string detail;  // option name / token text
};

std::string TagText(const PErr& e) {
    switch (e.tag) {
        case Tag::kBadFormatToken: return "BadFormatTokenError";
        case Tag::kUnknownOption: return "UnknownOptionError:" + e.detail;
        case Tag::kBadFormatConversion: return "BadFormatConversionError";
        case Tag::kMissingRequired: return "MissingRequiredOptionError";
        case Tag::kRepeatedOption: return "RepeatedOptionError";
        case Tag::kHelpRequested: return "HelpRequestedError";
        case Tag::kVersionRequested: return "VersionRequestedError";
    }
    return "";
}

struct Bound {
    bool bound = false;
    bool bool_value = false;
    int int_value = 0;
    double double_value = 0.0;
    std::string raw;
};

struct ParseState {
    std::vector<PErr> errors;
    std::map<std::string, Bound> bound;  // keyed by option name
};

// --- Tokenizer.Tokenize + unknown-name post-pass (v2.9.1) ---
void Tokenize(const OptionSet& model, const std::vector<std::string>& args,
              std::vector<Tok>& toks, std::vector<PErr>& errors) {
    for (const std::string& arg : args) {
        if (arg.rfind("-", 0) != 0) {
            toks.push_back({TokKind::kValue, arg});
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            if (arg.size() == 2) continue;  // "--" vanishes (EnableDashDash=false)
            const std::string text = arg.substr(2);
            const std::size_t eq = text.find('=');
            if (eq == std::string::npos) {
                // "--name" -> Name("name")
                toks.push_back({TokKind::kName, text});
                continue;
            }
            if (eq == 0) {
                // "--=" / "--=x": there is no name before the '=', so the whole
                // remainder becomes the NAME ("=", "=x") — it is then reported
                // as an unknown option, NOT BadFormatTokenError.
                // (Oracle-verified against CommandLineParser 2.9.1 for both the
                // verb and the player option sets; the earlier verb fuzz corpus
                // simply never contained this token.)
                toks.push_back({TokKind::kName, text});
                continue;
            }
            // Regex "^([^=]+)=([^ ].*)$": name must not contain '=' and the
            // value must be non-empty and not start with a space.
            const std::string name = text.substr(0, eq);
            const std::string value = text.substr(eq + 1);
            if (name.find('=') == std::string::npos && !value.empty() && value[0] != ' ') {
                toks.push_back({TokKind::kName, name});
                toks.push_back({TokKind::kValue, value});
            } else {
                errors.push_back({Tag::kBadFormatToken, arg});
            }
            continue;
        }
        // Short form: a lone "-" and "-<digit>..." are Values; otherwise the
        // NAME is the SINGLE character after '-' and the rest of the argument
        // becomes a Value token — so "-abc" is Name("a") + Value("bc") and
        // "-Infinity" is Name("I") + Value("nfinite"). (Oracle-verified; the
        // earlier verb fuzz corpus had no multi-character short tokens, so this
        // only shows up as UnknownOptionError:a where "abc" was reported before.)
        if (arg.size() == 1) {
            toks.push_back({TokKind::kValue, arg});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(arg[1]))) {
            toks.push_back({TokKind::kValue, arg});
            continue;
        }
        toks.push_back({TokKind::kName, arg.substr(1, 1)});
        if (arg.size() > 2) {
            toks.push_back({TokKind::kValue, arg.substr(2)});
        }
    }
    // Post-pass: unknown names are removed from the stream and reported
    // (their value tokens remain — those become loose values). Errors are
    // deduplicated per name (oracle-verified: "--zz --zz --zz" -> one error).
    //
    // Removal happens HERE, before partitioning: CommandLineParser 2.9.1's
    // tokenizer drops unknown names, so they never clear a pending scalar name.
    // (Verified: "--stretch --wallpaper-pause-event Auto" binds stretch := "Auto"
    // and reports a conversion error, rather than leaving stretch unbound.)
    std::vector<Tok> kept;
    kept.reserve(toks.size());
    std::unordered_set<std::string> reported_unknown;
    for (const Tok& t : toks) {
        if (t.kind == TokKind::kName && !FindOption(model, t.text)) {
            if (reported_unknown.insert(t.text).second) {
                errors.push_back({Tag::kUnknownOption, t.text});
            }
        } else {
            kept.push_back(t);
        }
    }
    toks.swap(kept);
}

// --- TokenPartitioner (all options Scalar) + ForScalar Group(2) pairing ---
void PartitionAndBind(const OptionSet& model, const std::vector<Tok>& toks,
                      ParseState& st) {
    // NAME tokens per option, in first-appearance order (switches and
    // EnforceSingle both need this).
    std::unordered_map<std::string, int> name_counts;
    std::vector<std::string> name_order;
    for (const Tok& t : toks) {  // unknown names were removed above
        if (t.kind == TokKind::kName) {
            if (name_counts[t.text]++ == 0) name_order.push_back(t.text);
        }
    }

    // Mirrors CommandLineParser's TokenPartitioner.PartitionTokensByType.
    //
    //  * a Switch NAME goes to its own bucket and resets the scan state;
    //  * a Scalar NAME becomes the pending name (and joins the scalar stream);
    //  * a VALUE joins the scalar stream only when a scalar NAME is pending,
    //    otherwise it is a loose value that no Lively option consumes.
    //
    // This is deliberately NOT "group the whole token stream in twos": the
    // pending name only accepts the *immediately* following token, which is why
    //   "--stretch --verbose-log --path a"  -> stretch := "path" (conversion
    //                                             fails) and --path unbound,
    //   "--path --verbose-log a"            -> --path unbound ("a" is loose),
    //   "--volume --verbose-log 5"          -> --volume unbound (5 is loose).
    std::vector<Tok> scalars;
    bool scalar_name_pending = false;
    for (const Tok& t : toks) {
        if (t.kind == TokKind::kName) {
            const OptionSpec* spec = FindOption(model, t.text);  // never null here
            if (spec != nullptr && spec->kind == OptionKind::boolean_switch) {
                scalar_name_pending = false;
            } else {
                scalars.push_back(t);
                scalar_name_pending = true;
            }
            continue;
        }
        if (scalar_name_pending) {
            scalars.push_back(t);
            scalar_name_pending = false;
        }
        // else: loose value -> ignored (Lively's option sets define no Value specs)
    }

    // Group(2): trailing incomplete group silently dropped. A pair key may be
    // a value token (it then matches no option).
    std::map<std::string, std::vector<std::string>> option_values;  // stream order
    for (std::size_t i = 0; i + 1 < scalars.size(); i += 2) {
        if (scalars[i].kind == TokKind::kName) {
            option_values[scalars[i].text].push_back(scalars[i + 1].text);
        }
        // A value-as-key pair matches no option (MatchName fails) -> ignored.
    }

    // --- OptionMapper.MapValues (declaration order) ---
    for (std::size_t i = 0; i < model.count; ++i) {
        const OptionSpec& spec = model.options[i];

        if (spec.kind == OptionKind::boolean_switch) {
            // TargetType.Switch: a bare NAME binds true; a value (attached or
            // via '=') is consumed but ignored.
            const auto nc = name_counts.find(spec.name);
            if (nc != name_counts.end() && nc->second > 0) {
                Bound b;
                b.bound = true;
                b.bool_value = true;
                b.raw = "true";
                st.bound[spec.name] = b;
            }
            continue;
        }

        const auto it = option_values.find(spec.name);
        if (it == option_values.end() || it->second.empty()) continue;  // no kvp: unbound, no error
        const std::string& last = it->second.back();  // ChangeTypeScalar(values.Last())
        Bound b;
        b.raw = last;
        bool ok = true;
        switch (spec.kind) {
            case OptionKind::integer:
                ok = parse_int_strict(last, b.int_value);
                break;
            case OptionKind::boolean:
                ok = parse_bool_strict(last, b.bool_value);
                break;
            case OptionKind::number: {
                double parsed = 0.0;
                ok = parse_double_strict(last, parsed);
                if (ok) {
                    b.double_value = parsed;
                    // Normalise ("1,5" -> "15", "1e3" -> "1000") so callers
                    // never re-parse the raw text and lose the conversion.
                    char buffer[64];
                    const auto written = std::to_chars(buffer, buffer + sizeof(buffer), parsed);
                    b.raw.assign(buffer, written.ptr);
                }
                break;
            }
            case OptionKind::enumeration: {
                // C# Enum.Parse(type, value) — ignoreCase DEFAULTS TO FALSE, so
                // "dark" fails where "Dark" succeeds (oracle-verified). A
                // numeric token is accepted only when it names a *defined*
                // member (Enum.Parse rejects undefined ordinals for non-flags
                // enums). The bound value is normalised to the ordinal.
                std::string trimmed = last;
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
                    trimmed.erase(trimmed.begin());
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
                    trimmed.pop_back();

                ok = false;
                int ordinal = 0;
                if (parse_int_strict(trimmed, ordinal)) {
                    if (ordinal >= 0 && static_cast<std::size_t>(ordinal) < spec.enum_count) {
                        b.raw = std::to_string(ordinal);
                        ok = true;
                    }
                    break;
                }
                for (std::size_t member = 0; member < spec.enum_count; ++member) {
                    if (trimmed == spec.enum_names[member]) {
                        b.raw = std::to_string(member);
                        ok = true;
                        break;
                    }
                }
                break;
            }
            case OptionKind::boolean_switch:
            case OptionKind::text:
                break;  // unreachable / raw value is the result
        }
        if (ok) {
            b.bound = true;
            st.bound[spec.name] = b;
        } else {
            st.errors.push_back({Tag::kBadFormatConversion, spec.name});
        }
    }

    // --- EnforceSingle: counts NAME tokens per option, but only for options
    // that successfully bound (sp.Value.IsJust()) — a conversion-failed option
    // never yields RepeatedOptionError. Order: first appearance in tokens.
    for (const std::string& name : name_order) {
        const auto b = st.bound.find(name);
        if (b != st.bound.end() && b->second.bound && name_counts[name] > 1) {
            st.errors.push_back({Tag::kRepeatedOption, name});
        }
    }

    // --- EnforceRequired (declaration order) ---
    for (std::size_t i = 0; i < model.count; ++i) {
        const OptionSpec& spec = model.options[i];
        if (!spec.required) continue;
        const auto b = st.bound.find(spec.name);
        if (b == st.bound.end() || !b->second.bound) {
            st.errors.push_back({Tag::kMissingRequired, spec.name});
        }
    }
}

// Canonical tag order (tokenizer -> conversion -> required -> repeated);
// stable keeps within-tag order (declaration/token order).
std::string JoinErrors(std::vector<PErr>& errors) {
    if (errors.empty()) return {};
    std::stable_sort(errors.begin(), errors.end(), [](const PErr& a, const PErr& b) {
        return static_cast<int>(a.tag) < static_cast<int>(b.tag);
    });

    // The library de-duplicates errors whose rendered message is identical.
    // Two copies of the SAME malformed token collapse to one BadFormatTokenError,
    // and a bad conversion on one option mentioned twice gives one error — but
    // two different missing-required options (different names) both survive, and
    // so do two different repeated options. `detail` carries that discriminator
    // (the token text or the option name), so key on it rather than on the
    // canonical text, which deliberately omits names.
    std::set<std::pair<int, std::string>> seen;
    std::string joined;
    for (const auto& e : errors) {
        if (!seen.insert({static_cast<int>(e.tag), e.detail}).second) continue;
        if (!joined.empty()) joined += ",";
        joined += TagText(e);
    }
    return joined;
}

}  // namespace

OptionValues ParseOptions(const OptionSpec* options, std::size_t count,
                          const std::vector<std::string>& args) {
    OptionValues result;

    // Parser.Default has no verbs, so PreprocessorGuards apply to the first
    // argument only (HelpRequestedError / VersionRequestedError).
    if (!args.empty() && (args[0] == "--help" || args[0] == "--version")) {
        result.error = args[0] == "--help" ? "HelpRequestedError" : "VersionRequestedError";
        return result;
    }

    const OptionSet set{"", options, count};
    ParseState st;
    std::vector<Tok> toks;
    Tokenize(set, args, toks, st.errors);
    PartitionAndBind(set, toks, st);

    if (!st.errors.empty()) {
        result.error = JoinErrors(st.errors);
        return result;
    }

    for (const auto& [name, bound] : st.bound) result.values[name] = bound.raw;
    result.success = true;
    return result;
}

ParseResult ParseCommandLine(const std::vector<std::string>& args) {
    ParseResult result;

    // PreprocessorGuards + InstanceChooser: "--help"/"--version" as the first
    // post-verb argument fail the parse. Empty argv -> default verb OK.
    std::string verb = "app";
    std::vector<std::string> rest = args;
    if (!args.empty()) {
        const std::string& first = args[0];
        if (first == "--help") {
            result.errors.push_back("HelpRequestedError");
            result.success = false;
            return result;
        }
        if (first == "--version") {
            result.errors.push_back("VersionRequestedError");
            result.success = false;
            return result;
        }
        if (!first.empty() && first[0] != '-' && FindModel(first) != nullptr) {
            verb = first;
            rest.assign(args.begin() + 1, args.end());
            if (!rest.empty() && rest[0] == "--help") {
                result.errors.push_back("HelpRequestedError");
                result.success = false;
                return result;
            }
            if (!rest.empty() && rest[0] == "--version") {
                result.errors.push_back("VersionRequestedError");
                result.success = false;
                return result;
            }
        }
        // Unknown first token -> default verb with FULL argv (MatchDefaultVerb).
    }

    const OptionSet* model = FindModel(verb);
    ParseState st;
    std::vector<Tok> toks;
    Tokenize(*model, rest, toks, st.errors);
    PartitionAndBind(*model, toks, st);

    if (!st.errors.empty()) {
        result.errors.push_back(JoinErrors(st.errors));
        result.success = false;
        return result;
    }

    auto get_bool = [&](const char* name, std::optional<bool>& dst) {
        const auto it = st.bound.find(name);
        if (it != st.bound.end() && it->second.bound) dst = it->second.bool_value;
    };
    auto get_int = [&](const char* name, std::optional<int>& dst) {
        const auto it = st.bound.find(name);
        if (it != st.bound.end() && it->second.bound) dst = it->second.int_value;
    };
    auto get_str = [&](const char* name, std::optional<std::string>& dst) {
        const auto it = st.bound.find(name);
        if (it != st.bound.end() && it->second.bound) dst = it->second.raw;
    };

    if (verb == "app") {
        AppOptions o;
        get_bool("showApp", o.show_app);
        get_bool("showIcons", o.show_icons);
        get_str("volume", o.volume);
        get_bool("play", o.play);
        get_bool("startup", o.startup);
        get_bool("shutdown", o.shutdown_app);
        get_bool("restart", o.restart_app);
        get_str("layout", o.wallpaper_arrangement);
        result.options = std::move(o);
    } else if (verb == "setwp") {
        SetWallpaperOptions o;
        const auto f = st.bound.find("file");
        if (f != st.bound.end()) o.file = f->second.raw;
        get_int("monitor", o.monitor);
        result.options = std::move(o);
    } else if (verb == "closewp") {
        CloseWallpaperOptions o;
        o.monitor = st.bound.at("monitor").int_value;
        result.options = o;
    } else if (verb == "seekwp") {
        SeekWallpaperOptions o;
        o.param = st.bound.at("value").raw;
        get_int("monitor", o.monitor);
        result.options = std::move(o);
    } else if (verb == "setprop") {
        CustomiseWallpaperOptions o;
        o.param = st.bound.at("property").raw;
        get_int("monitor", o.monitor);
        result.options = std::move(o);
    } else if (verb == "screensaver") {
        ScreenSaverOptions o;
        get_int("preview", o.preview);
        get_bool("configure", o.configure);
        get_bool("show", o.show);
        get_bool("showExclusive", o.show_exclusive);
        get_bool("fadeIn", o.is_fade_in);
        result.options = std::move(o);
    } else if (verb == "screenshot") {
        ScreenshotOptions o;
        const auto f = st.bound.find("file");
        if (f != st.bound.end()) o.file = f->second.raw;
        get_int("monitor", o.monitor);
        result.options = std::move(o);
    }

    result.success = true;
    return result;
}

}  // namespace lively::utility
