// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_MATH_HPP
#define DASMIG_CORE_MATH_HPP

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

} // namespace dasmig::core::math


#endif // DASMIG_CORE_MATH_HPP
