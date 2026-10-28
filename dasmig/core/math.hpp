// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_MATH_HPP
#define DASMIG_CORE_MATH_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

/// @file math.hpp
/// @brief Floating-point functions that give bit-identical results on every
///        compiler, standard library and platform.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// `std::exp` and `std::log` are not required to be correctly rounded, and
/// glibc, the MSVC runtime, Apple's libm and Emscripten's musl differ in the
/// last bit for some inputs. The functions here use only operations IEEE 754
/// defines exactly (+, -, *, /, sqrt, floor, frexp, ldexp), so the same input
/// gives the same bits everywhere. They are accurate to a few ulp, which is
/// what sampling needs; they are not replacements for `<cmath>` in general.
///
/// Exactness also needs every operation rounded on its own. Compilers may fuse
/// `a * b + c` into one FMA instruction (GCC in its default GNU mode, clang
/// with `-ffp-contract=fast`, ARM64 targets), which rounds once instead of
/// twice. Pragmas do not prevent this reliably (clang ignores them under
/// `-ffp-contract=fast`), so every product that feeds an addition in the core
/// passes through fenced(), which the compiler cannot see through. Fast-math
/// modes, which also reassociate, are refused at compile time.

#ifdef __FAST_MATH__
#error "dasmig-core needs IEEE 754 arithmetic: do not compile with -ffast-math"
#endif
// clang-cl defines _M_FP_CONTRACT by default; fenced() covers clang.
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_FP_FAST) || defined(_M_FP_CONTRACT))
#error "dasmig-core needs IEEE 754 arithmetic: do not compile with /fp:fast or /fp:contract"
#endif
#if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1930
#error "dasmig-core needs Visual Studio 2022 or later (earlier /fp:precise contracts)"
#endif
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "dasmig-core needs FLT_EVAL_METHOD == 0 (no x87 extended precision; use SSE2)"
#endif

namespace dasmig::core::math
{

/// @brief Return @p x unchanged, hiding its value from the optimizer.
///
/// Wrap a product in this before adding to it, so the compiler cannot fuse
/// the two into a fused multiply-add: `fenced(a * b) + c`. Costs nothing on
/// x86-64 and ARM64 (an empty asm statement); WebAssembly has no scalar FMA,
/// and MSVC (VS2022, /fp:precise) does not contract, so neither needs it.
///
/// @param x A value, typically a product.
/// @return @p x, rounded to double.
[[nodiscard]] inline double fenced(double x) noexcept
{
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    __asm__("" : "+x"(x)); // NOLINT(hicpp-no-assembler): empty optimization barrier
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__)
    __asm__("" : "+w"(x)); // NOLINT(hicpp-no-assembler): empty optimization barrier
#elif defined(__wasm__) || (defined(_MSC_VER) && !defined(__clang__))
#else
    volatile double v = x;
    x = v;
#endif
    return x;
}

/// @cond INTERNAL
namespace detail
{

// ln(2) split so that k * ln2_hi is exact for |k| < 2^20 (ln2_hi has its low
// 32 bits zero; ln2_hi + ln2_lo is ln(2) to about 2^-83).
inline constexpr double ln2_hi = 6.93147180369123816490e-01; // NOLINT(modernize-use-std-numbers): deliberately not ln(2)
inline constexpr double ln2_lo = 1.90821492927058770002e-10;
inline constexpr double inv_ln2 = std::numbers::log2e;
inline constexpr double sqrt_half = 7.07106781186547524401e-01;

// Largest and smallest arguments for which exp is finite and non-zero.
inline constexpr double exp_max = 7.09782712893383973096e+02;
inline constexpr double exp_min = -7.45133219101941108420e+02;

// 1/i! for i = 0..13: the Taylor series of exp on |r| <= ln(2)/2.
inline constexpr std::size_t exp_terms = 14;
inline constexpr std::array<double, exp_terms> exp_coeff = [] {
    std::array<double, exp_terms> c{};
    double factorial = 1.0;
    double i = 0.0;
    for (auto& coeff : c)
    {
        if (i > 0.0) factorial *= i;
        coeff = 1.0 / factorial;
        i += 1.0;
    }
    return c;
}();

// 2/(2k+1) for k = 0..9: log(m) = sum of these times s^(2k+1), s = (m-1)/(m+1),
// for m in [sqrt(1/2), sqrt(2)), where |s| <= 0.1716.
inline constexpr std::size_t log_terms = 10;
inline constexpr std::array<double, log_terms> log_coeff = [] {
    std::array<double, log_terms> c{};
    double odd = 1.0;
    for (auto& coeff : c)
    {
        coeff = 2.0 / odd;
        odd += 2.0;
    }
    return c;
}();

} // namespace detail
/// @endcond

/// @brief Natural exponential, bit-identical on every platform.
///
/// Reduces x = k ln(2) + r with |r| <= ln(2)/2, evaluates a degree-13 Taylor
/// polynomial in r by Horner's rule and scales by 2^k.
///
/// @param x Exponent.
/// @return e^x; +inf above about 709.78, 0 below about -745.13, NaN for NaN.
[[nodiscard]] inline double exp(double x) noexcept
{
    if (std::isnan(x)) return x;
    if (x > detail::exp_max) return std::numeric_limits<double>::infinity();
    if (x < detail::exp_min) return 0.0;

    const double k = std::floor(fenced(x * detail::inv_ln2) + 0.5);
    const double r = (x - fenced(k * detail::ln2_hi)) - fenced(k * detail::ln2_lo);

    // Horner's rule from the highest coefficient down.
    auto coeff = detail::exp_coeff.crbegin();
    double p = *coeff;
    for (++coeff; coeff != detail::exp_coeff.crend(); ++coeff)
    {
        p = fenced(p * r) + *coeff;
    }
    return std::ldexp(p, static_cast<int>(k));
}

/// @brief Natural logarithm, bit-identical on every platform.
///
/// Splits x = m 2^e with m in [sqrt(1/2), sqrt(2)), evaluates
/// log(m) = 2 atanh((m-1)/(m+1)) as an odd series and adds e ln(2).
///
/// @param x Argument.
/// @return ln(x); -inf for 0, +inf for +inf, NaN for negative or NaN.
[[nodiscard]] inline double log(double x) noexcept
{
    if (std::isnan(x) || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return -std::numeric_limits<double>::infinity();
    if (std::isinf(x)) return x;

    int e = 0;
    double m = std::frexp(x, &e); // x = m * 2^e, m in [0.5, 1)
    if (m < detail::sqrt_half)
    {
        m *= 2.0;
        --e;
    }
    const double f = m - 1.0; // exact: m is within a factor of 2 of 1
    const double s = f / (m + 1.0);
    const double z = s * s;

    auto coeff = detail::log_coeff.crbegin();
    double p = *coeff;
    for (++coeff; coeff != detail::log_coeff.crend(); ++coeff)
    {
        p = fenced(p * z) + *coeff;
    }
    const double log_m = fenced(s * p);
    const auto k = static_cast<double>(e);
    return fenced(k * detail::ln2_hi) + (log_m + fenced(k * detail::ln2_lo));
}

/// @brief The sine and cosine of one angle (sin_cos_deg()).
struct sine_cosine
{
    double sin; ///< The sine.
    double cos; ///< The cosine.
};

/// @brief Sine and cosine of an angle in degrees, bit-identical on every
///        platform.
///
/// Reduces by the nearest multiple of 90 degrees, q = round(degrees / 90),
/// so x = (degrees - 90 q) pi / 180 is within pi/4, evaluates the Taylor
/// series of sin to x^17 and of cos to x^16 by Horner's rule in x^2 (error
/// below 1e-17 on |x| <= pi/4), and swaps and negates by q mod 4. Working in
/// degrees keeps the reduction exact: 90 q is exact where a multiple of pi is
/// not.
///
/// @param degrees The angle. Accurate while 90 q is exact (|degrees| below
///        about 8e16); multiples of 90 give exact 0 and 1.
/// @return sin and cos; both NaN for NaN or infinity.
[[nodiscard]] inline sine_cosine sin_cos_deg(double degrees) noexcept
{
    if (!std::isfinite(degrees))
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {.sin = nan, .cos = nan};
    }
    const double q = std::round(degrees / 90.0);
    const double x = (degrees - fenced(90.0 * q)) * (std::numbers::pi / 180.0); // |x| <= pi/4
    const double x2 = x * x;
    double s = 1.0 / 355687428096000.0;
    double c = 1.0 / 20922789888000.0;
    constexpr std::array<double, 8> sin_terms{-1.0 / 1307674368000.0, 1.0 / 6227020800.0,
                                              -1.0 / 39916800.0,      1.0 / 362880.0,
                                              -1.0 / 5040.0,          1.0 / 120.0,
                                              -1.0 / 6.0,             1.0};
    constexpr std::array<double, 8> cos_terms{-1.0 / 87178291200.0, 1.0 / 479001600.0,
                                              -1.0 / 3628800.0,     1.0 / 40320.0,
                                              -1.0 / 720.0,         1.0 / 24.0,
                                              -1.0 / 2.0,           1.0};
    for (const double term : sin_terms) s = fenced(s * x2) + term;
    for (const double term : cos_terms) c = fenced(c * x2) + term;
    s *= x;
    const double m = std::fmod(q, 4.0); // exact; -3 to 3
    switch (static_cast<int>(m < 0.0 ? m + 4.0 : m))
    {
    case 1: return {.sin = c, .cos = -s};
    case 2: return {.sin = -s, .cos = -c};
    case 3: return {.sin = -c, .cos = s};
    default: return {.sin = s, .cos = c};
    }
}

/// @brief Sine of an angle in degrees: sin_cos_deg(degrees).sin.
/// @param degrees The angle.
/// @return The sine; NaN for NaN or infinity.
[[nodiscard]] inline double sin_deg(double degrees) noexcept { return sin_cos_deg(degrees).sin; }

/// @brief Cosine of an angle in degrees: sin_cos_deg(degrees).cos.
/// @param degrees The angle.
/// @return The cosine; NaN for NaN or infinity.
[[nodiscard]] inline double cos_deg(double degrees) noexcept { return sin_cos_deg(degrees).cos; }

/// @brief Arctangent in radians, bit-identical on every platform.
///
/// Folds x to |x| <= 1 (atan x = pi/2 - atan(1/x)), then above tan(pi/8)
/// moves to (x - 1) / (x + 1) (adding pi/4), and sums the odd Taylor series
/// to x^51 by Horner's rule in x^2 (error below 1e-17 on |x| <= tan(pi/8)).
///
/// @param x The tangent.
/// @return atan(x) in [-pi/2, pi/2]; NaN for NaN; +0 for -0.
[[nodiscard]] inline double atan(double x) noexcept
{
    const bool negative = x < 0.0;
    if (negative) x = -x;
    const bool inverted = x > 1.0;
    if (inverted) x = 1.0 / x;
    double base = 0.0;
    if (x > 0.41421356237309503) // tan(pi / 8)
    {
        x = (x - 1.0) / (x + 1.0);
        base = std::numbers::pi / 4.0;
    }
    const double x2 = fenced(x * x);
    double sum = 0.0;
    for (int n = 25; n >= 0; --n) sum = fenced(sum * x2) + ((n % 2 == 0 ? 1.0 : -1.0) / ((2.0 * n) + 1.0));
    double r = base + fenced(sum * x);
    if (inverted) r = (std::numbers::pi / 2.0) - r;
    return negative ? -r : r;
}

/// @brief The angle of the point (x, y) in degrees, bit-identical on every
///        platform.
///
/// atan(y / x) in degrees, moved by 180 degrees when x is negative; 90 or
/// -90 on the y axis.
///
/// @param y The ordinate.
/// @param x The abscissa.
/// @return The angle in (-180, 180]; 0 for (0, 0); NaN when either is NaN.
[[nodiscard]] inline double atan2_deg(double y, double x) noexcept
{
    constexpr double deg = 180.0 / std::numbers::pi;
    if (std::isnan(x) || std::isnan(y)) return std::numeric_limits<double>::quiet_NaN();
    if (x > 0.0) return atan(y / x) * deg;
    if (x < 0.0) return fenced(atan(y / x) * deg) + (y >= 0.0 ? 180.0 : -180.0);
    if (y == 0.0) return 0.0;
    return y > 0.0 ? 90.0 : -90.0;
}

/// @brief Arcsine in degrees: atan2_deg(x, sqrt(1 - x^2)).
/// @param x The sine; values beyond [-1, 1] give 90 or -90.
/// @return The angle in [-90, 90]; NaN for NaN.
[[nodiscard]] inline double asin_deg(double x) noexcept
{
    if (std::isnan(x)) return x;
    return atan2_deg(x, std::sqrt(std::max(0.0, 1.0 - fenced(x * x))));
}

/// @brief Arccosine in degrees: atan2_deg(sqrt(1 - x^2), x).
/// @param x The cosine; values beyond [-1, 1] give 0 or 180.
/// @return The angle in [0, 180]; NaN for NaN.
[[nodiscard]] inline double acos_deg(double x) noexcept
{
    if (std::isnan(x)) return x;
    return atan2_deg(std::sqrt(std::max(0.0, 1.0 - fenced(x * x))), x);
}

} // namespace dasmig::core::math


#endif // DASMIG_CORE_MATH_HPP
