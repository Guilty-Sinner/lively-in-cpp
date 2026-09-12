#pragma once
// .NET conversion primitives that C# call sites depend on for *value* semantics,
// not just syntax. Kept in one place because getting these wrong is invisible
// until a control silently sends the wrong number to a running wallpaper.
//
// Convert.ToInt32(double) is the interesting one. It is NOT a C++ static_cast or
// std::lround: .NET rounds to the nearest integer with ties going to the EVEN
// value (banker's rounding), so 2.5 -> 2, 3.5 -> 4, -0.5 -> 0. A slider whose
// Step is a whole number is sent to mpv through this conversion
// (VideoMpvPlayer.SetLivelyProperties), so a halfway value reaching a video
// wallpaper would otherwise be off by one from the C# app.
//
// Out-of-range input is a real difference: C# throws OverflowException, which
// VideoMpvPlayer catches (`catch (OverflowException)`) and logs. The port
// reports it through the optional return instead of aborting a callback.

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace lively::common {

// Convert.ToInt32(double): nearest integer, ties to even. nullopt where the C#
// would throw OverflowException (NaN, infinity, or outside int range).
inline std::optional<std::int32_t> convert_to_int32(double value) {
    if (std::isnan(value) || std::isinf(value))
        return std::nullopt;
    if (value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;
    // std::nearbyint honours the current rounding mode, which is round-to-nearest
    // with ties to even by default — exactly Convert.ToInt32's rule.
    return static_cast<std::int32_t>(std::nearbyint(value));
}

// Same conversion where the caller wants the C# exception behaviour: throws
// std::overflow_error where C# throws OverflowException.
inline std::int32_t convert_to_int32_or_throw(double value) {
    auto converted = convert_to_int32(value);
    if (!converted)
        throw std::overflow_error("Value was either too large or too small for an Int32.");
    return *converted;
}

// The LivelyProperties slider rule: `isFraction = (Step % 1) != 0`.
// mpv is strongly typed, so a whole-number slider is sent as an int and a
// fractional one as a double (VideoMpvPlayer.SetLivelyProperties).
inline bool slider_step_is_fraction(double step) {
    return std::fmod(step, 1.0) != 0.0;
}

} // namespace lively::common
