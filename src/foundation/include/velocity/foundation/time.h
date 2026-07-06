#pragma once
// Rational time types — the model-layer time contract from docs/02 §4:
// all timeline positions are integer Ticks at kTickRate; media-stream
// positions are integers in the stream's own rational timebase and are
// converted explicitly. Floating-point time is banned in the model.

#include <cstdint>
#include <intrin.h>
#include <numeric>

namespace velocity {

// Ticks per second. 48000 divides evenly into audio samples at 48 kHz and
// into frame durations for 24/25/30/48/50/60 fps; NTSC rates (24000/1001
// etc.) stay exact through rational conversion helpers below.
inline constexpr std::int64_t kTickRate = 48000;

using Tick = std::int64_t;

struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;

    constexpr Rational() = default;
    constexpr Rational(std::int64_t n, std::int64_t d) : num(n), den(d) {}

    [[nodiscard]] Rational normalized() const {
        std::int64_t n = num, d = den;
        if (d < 0) {
            n = -n;
            d = -d;
        }
        const std::int64_t g = std::gcd(n < 0 ? -n : n, d);
        return g > 1 ? Rational{n / g, d / g} : Rational{n, d};
    }

    [[nodiscard]] constexpr bool valid() const { return den != 0; }
};

namespace detail {
// Exact comparison of a*b vs c*d in 128-bit, MSVC x64.
inline int cmpMul128(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d) {
    std::int64_t hi1 = 0, hi2 = 0;
    const std::int64_t lo1 = _mul128(a, b, &hi1);
    const std::int64_t lo2 = _mul128(c, d, &hi2);
    if (hi1 != hi2)
        return hi1 < hi2 ? -1 : 1;
    if (static_cast<std::uint64_t>(lo1) != static_cast<std::uint64_t>(lo2))
        return static_cast<std::uint64_t>(lo1) < static_cast<std::uint64_t>(lo2) ? -1 : 1;
    return 0;
}

// (value * num) / den with round-to-nearest-even-free floor/round semantics,
// exact in 128-bit. den must be > 0.
inline std::int64_t mulDiv(std::int64_t value, std::int64_t num, std::int64_t den, bool round) {
    std::int64_t hi = 0;
    std::int64_t lo = _mul128(value, num, &hi);
    if (round) {
        // add den/2 to the 128-bit product before dividing (toward +inf ties)
        const std::int64_t half = den / 2;
        const std::uint64_t oldLo = static_cast<std::uint64_t>(lo);
        lo = static_cast<std::int64_t>(oldLo + static_cast<std::uint64_t>(half));
        if (static_cast<std::uint64_t>(lo) < oldLo)
            ++hi;
    }
    std::int64_t rem = 0;
    std::int64_t q = _div128(hi, lo, den, &rem);
    // _div128 truncates toward zero; make negative results floor-divide.
    if (!round && rem != 0 && ((hi < 0) != (den < 0)))
        --q;
    return q;
}
} // namespace detail

// a <=> b for rationals with positive denominators, exact.
inline int compare(const Rational& a, const Rational& b) {
    return detail::cmpMul128(a.num, b.den, b.num, a.den);
}
inline bool operator==(const Rational& a, const Rational& b) { return compare(a, b) == 0; }
inline bool operator<(const Rational& a, const Rational& b) { return compare(a, b) < 0; }

// Media pts (integer in timebase tb) → engine ticks, floor semantics.
inline Tick ticksFromPts(std::int64_t pts, Rational tb) {
    // ticks = pts * tb.num * kTickRate / tb.den, done as two exact steps.
    return detail::mulDiv(pts, tb.num * kTickRate, tb.den, /*round=*/false);
}

// Engine tick → media pts in timebase tb, floor semantics.
inline std::int64_t ptsFromTicks(Tick t, Rational tb) {
    return detail::mulDiv(t, tb.den, tb.num * kTickRate, /*round=*/false);
}

// Frame index containing tick t for frame rate fps (frames/second, e.g. 30000/1001).
inline std::int64_t frameIndexFromTicks(Tick t, Rational fps) {
    return detail::mulDiv(t, fps.num, fps.den * kTickRate, /*round=*/false);
}

// First tick of frame index n at rate fps (ceil so frame boundaries never overlap).
inline Tick ticksFromFrameIndex(std::int64_t n, Rational fps) {
    std::int64_t hi = 0;
    std::int64_t lo = _mul128(n, fps.den * kTickRate, &hi);
    std::int64_t rem = 0;
    std::int64_t q = _div128(hi, lo, fps.num, &rem);
    if (rem != 0 && ((hi < 0) == (fps.num < 0)))
        ++q; // ceil for positive quotients
    return q;
}

// Duration of media at engine rate: seconds expressed as ticks, rounded.
inline Tick ticksFromSeconds(double seconds) {
    return static_cast<Tick>(seconds * static_cast<double>(kTickRate) + 0.5);
}

} // namespace velocity
