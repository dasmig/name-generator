// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_DISTRIBUTIONS_HPP
#define DASMIG_CORE_DISTRIBUTIONS_HPP

#include "math.hpp"
#include "rng.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/// @file distributions.hpp
/// @brief Random distributions with output fixed by the core, not by the
///        standard library.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// `std::uniform_int_distribution`, `std::normal_distribution`,
/// `std::discrete_distribution` and `std::shuffle` use different algorithms
/// in libc++, libstdc++ and the MSVC STL, so the same seed gives different
/// results on different platforms. Every algorithm here is specified exactly
/// (see each function) and is part of the core's output contract.
///
/// Each function consumes a fixed number of engine values for given
/// arguments, except where rejection sampling is documented.


namespace dasmig::core
{

/// @cond INTERNAL
namespace detail
{

// High 64 bits of the 128-bit product a * b, without a 128-bit type.
// The product commutes, so swapped arguments cannot be a mistake.
[[nodiscard]] constexpr std::uint64_t mul_hi64(
    std::uint64_t a, std::uint64_t b) noexcept // NOLINT(bugprone-easily-swappable-parameters)
{
    constexpr std::uint64_t mask = 0xffffffffULL;
    const std::uint64_t a_lo = a & mask;
    const std::uint64_t a_hi = a >> 32U;
    const std::uint64_t b_lo = b & mask;
    const std::uint64_t b_hi = b >> 32U;
    const std::uint64_t p0 = a_lo * b_lo;
    const std::uint64_t p1 = a_lo * b_hi;
    const std::uint64_t p2 = a_hi * b_lo;
    const std::uint64_t p3 = a_hi * b_hi;
    const std::uint64_t mid = (p0 >> 32U) + (p1 & mask) + (p2 & mask);
    return p3 + (p1 >> 32U) + (p2 >> 32U) + (mid >> 32U);
}

} // namespace detail
/// @endcond

/// @brief Uniform integer in [0, n).
///
/// Lemire's multiply-and-reject method (2019): the result is the high 64 bits
/// of `r() * n`, redrawn while the low 64 bits are below `2^64 mod n`.
/// Unbiased; consumes one value, rarely more.
///
/// @param r Engine.
/// @param n Exclusive upper bound.
/// @return A value in [0, n).
/// @throws std::invalid_argument If @p n is 0.
[[nodiscard]] constexpr std::uint64_t bounded(rng& r, std::uint64_t n)
{
    if (n == 0)
    {
        throw std::invalid_argument("bounded: n must be positive");
    }
    std::uint64_t x = r();
    std::uint64_t low = x * n;
    if (low < n)
    {
        const std::uint64_t threshold = (0 - n) % n;
        while (low < threshold)
        {
            x = r();
            low = x * n;
        }
    }
    return detail::mul_hi64(x, n);
}

/// @brief Uniform integer in [lo, hi], for any integral type except bool.
///
/// `lo + bounded(r, hi - lo + 1)` computed in 64-bit unsigned arithmetic;
/// the full 64-bit range takes one raw engine value.
///
/// @tparam T Integral type.
/// @param r Engine.
/// @param lo Inclusive lower bound.
/// @param hi Inclusive upper bound.
/// @return A value in [lo, hi].
/// @throws std::invalid_argument If @p lo > @p hi.
template <std::integral T>
    requires(!std::same_as<T, bool>)
[[nodiscard]] constexpr T uniform_int(rng& r, T lo, T hi)
{
    if (lo > hi)
    {
        throw std::invalid_argument("uniform_int: lo > hi");
    }
    const std::uint64_t span =
        static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo);
    const std::uint64_t offset =
        span == std::numeric_limits<std::uint64_t>::max() ? r()
                                                         : bounded(r, span + 1);
    return static_cast<T>(static_cast<std::uint64_t>(lo) + offset);
}

/// @brief Uniform double in [0, 1).
///
/// The top 53 bits of one engine value, times 2^-53. Exact.
///
/// @param r Engine.
/// @return A multiple of 2^-53 in [0, 1).
[[nodiscard]] constexpr double uniform01(rng& r) noexcept
{
    return static_cast<double>(r() >> 11U) * 0x1.0p-53;
}

/// @cond INTERNAL
namespace detail
{

// The weighted choice shared by discrete and weighted_table: the first index
// whose cumulative weight exceeds uniform01(r) * total. Rounding can make the
// target reach the total; then the last index with a positive weight.
[[nodiscard]] inline std::size_t pick_cumulative(rng& r,
                                                 const std::vector<double>& cumulative,
                                                 std::size_t last_positive)
{
    const double target = uniform01(r) * cumulative.back();
    const auto it = std::ranges::upper_bound(cumulative, target);
    if (it == cumulative.end()) return last_positive;
    return static_cast<std::size_t>(it - cumulative.begin());
}

} // namespace detail
/// @endcond

/// @brief Uniform double in [lo, hi).
///
/// `lo + (hi - lo) * uniform01(r)`, replaced by the largest double below
/// @p hi if rounding reaches @p hi. Returns @p lo when lo == hi. Consumes one
/// value.
///
/// @param r Engine.
/// @param lo Inclusive lower bound.
/// @param hi Exclusive upper bound.
/// @return A value in [lo, hi), or lo when lo == hi.
/// @throws std::invalid_argument If a bound is not finite, lo > hi, or
///         hi - lo overflows.
[[nodiscard]] inline double uniform_real(rng& r, double lo, double hi)
{
    const double width = hi - lo;
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo <= hi) ||
        !std::isfinite(width))
    {
        throw std::invalid_argument("uniform_real: need finite lo <= hi");
    }
    const double u = uniform01(r);
    if (width == 0.0) return lo;
    const double x = lo + math::fenced(width * u);
    return x < hi ? x : std::nextafter(hi, lo);
}

/// @brief True with probability @p p.
///
/// `uniform01(r) < p`. Consumes one value, also for p = 0 and p = 1.
///
/// @param r Engine.
/// @param p Probability of true.
/// @return The outcome.
/// @throws std::invalid_argument If @p p is not in [0, 1].
[[nodiscard]] inline bool bernoulli(rng& r, double p)
{
    if (std::isnan(p) || p < 0.0 || p > 1.0)
    {
        throw std::invalid_argument("bernoulli: p must be in [0, 1]");
    }
    return uniform01(r) < p;
}

/// @brief Normally distributed double.
///
/// Marsaglia's polar method: draw u and v as `2 * uniform01(r) - 1` until
/// s = u*u + v*v is in (0, 1), then return
/// `mean + sd * (u * sqrt((-2 * log(s)) / s))`, with math::log. The second
/// variate (from v) is discarded so no state is kept between calls.
/// Consumes 2 values per attempt (on average 2.55 values).
///
/// @param r Engine.
/// @param mean Mean.
/// @param sd Standard deviation (0 returns @p mean).
/// @return The variate.
/// @throws std::invalid_argument If @p mean or @p sd is not finite, or
///         @p sd is negative.
[[nodiscard]] inline double normal(rng& r, double mean, double sd)
{
    if (!std::isfinite(mean) || !std::isfinite(sd) || sd < 0.0)
    {
        throw std::invalid_argument("normal: need finite mean and sd >= 0");
    }
    double u = 0.0;
    double s = 0.0;
    for (;;)
    {
        u = (2.0 * uniform01(r)) - 1.0;
        const double v = (2.0 * uniform01(r)) - 1.0;
        s = math::fenced(u * u) + math::fenced(v * v);
        if (s < 1.0 && s != 0.0) break;
    }
    const double factor = std::sqrt((-2.0 * math::log(s)) / s);
    return mean + math::fenced(sd * (u * factor));
}

/// @brief Log-normally distributed double.
///
/// `math::exp(normal(r, mu, sigma))`.
///
/// @param r Engine.
/// @param mu Mean of the underlying normal.
/// @param sigma Standard deviation of the underlying normal.
/// @return The variate (positive).
/// @throws std::invalid_argument As normal().
[[nodiscard]] inline double lognormal(rng& r, double mu, double sigma)
{
    return math::exp(normal(r, mu, sigma));
}

/// @brief Index chosen with probability proportional to a weight.
///
/// Keeps the cumulative sums of the weights (added left to right). A draw is
/// the first index whose cumulative sum exceeds `uniform01(r) * total`, so a
/// zero weight is never chosen. Consumes one value.
///
/// @code
/// dasmig::core::discrete eye_colour{{0.7, 0.2, 0.1}};
/// std::size_t i = eye_colour(r);
/// @endcode
class discrete
{
  public:
    /// @brief Build from weights.
    /// @param weights Non-negative, finite weights, at least one positive.
    /// @throws std::invalid_argument If the weights are empty, a weight is
    ///         negative or not finite, or they sum to 0 or overflow.
    explicit discrete(std::span<const double> weights)
    {
        if (weights.empty())
        {
            throw std::invalid_argument("discrete: no weights");
        }
        _cumulative.reserve(weights.size());
        double total = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            const double w = weights[i];
            if (!std::isfinite(w) || w < 0.0)
            {
                throw std::invalid_argument(
                    "discrete: weights must be finite and non-negative");
            }
            total += w;
            if (w > 0.0) _last_positive = i;
            _cumulative.push_back(total);
        }
        if (!(total > 0.0) || !std::isfinite(total))
        {
            throw std::invalid_argument(
                "discrete: weights must have a positive, finite sum");
        }
    }

    /// @brief Build from a list of weights.
    /// @param weights Non-negative, finite weights, at least one positive.
    /// @throws std::invalid_argument As the span constructor.
    discrete(std::initializer_list<double> weights)
        : discrete{std::span<const double>{weights.begin(), weights.size()}}
    {
    }

    /// @brief Draw an index.
    /// @param r Engine.
    /// @return An index in [0, size()) with a positive weight.
    [[nodiscard]] std::size_t operator()(rng& r) const
    {
        return detail::pick_cumulative(r, _cumulative, _last_positive);
    }

    /// @brief Number of weights.
    /// @return The number of indices.
    [[nodiscard]] std::size_t size() const noexcept { return _cumulative.size(); }

    /// @brief Sum of the weights.
    /// @return The total weight.
    [[nodiscard]] double total() const noexcept { return _cumulative.back(); }

    /// @brief Probability of an index.
    /// @param i Index.
    /// @return weight(i) / total().
    /// @throws std::out_of_range If @p i >= size().
    [[nodiscard]] double probability(std::size_t i) const
    {
        const double before = i == 0 ? 0.0 : _cumulative.at(i - 1);
        return (_cumulative.at(i) - before) / total();
    }

  private:
    std::vector<double> _cumulative;
    std::size_t _last_positive{0};
};

/// @brief Shuffle a random-access range in place.
///
/// Fisher-Yates from the end: for i = n-1 down to 1, swap element i with
/// element bounded(r, i + 1). Consumes n - 1 values (rarely more).
///
/// @param r Engine.
/// @param range Range to shuffle.
template <std::ranges::random_access_range R>
    requires std::ranges::sized_range<R> &&
             std::permutable<std::ranges::iterator_t<R>>
constexpr void shuffle(rng& r, R&& range) // NOLINT(cppcoreguidelines-missing-std-forward): permuted in place, never moved
{
    auto first = std::ranges::begin(range);
    const auto n = static_cast<std::uint64_t>(std::ranges::size(range));
    for (std::uint64_t i = n; i-- > 1;)
    {
        const std::uint64_t j = bounded(r, i + 1);
        std::ranges::iter_swap(first + static_cast<std::ptrdiff_t>(i),
                               first + static_cast<std::ptrdiff_t>(j));
    }
}

/// @brief Choose k distinct indices from [0, n), in random order.
///
/// Partial Fisher-Yates from the front: start from 0..n-1 and for i = 0 to
/// k-1 swap element i with element i + bounded(r, n - i); return the first k.
/// Consumes k values (rarely more). Allocates n indices.
///
/// @param r Engine.
/// @param n Population size.
/// @param k Sample size.
/// @return k distinct indices.
/// @throws std::invalid_argument If @p k > @p n.
[[nodiscard]] inline std::vector<std::size_t> sample_indices(rng& r,
                                                             std::size_t n,
                                                             std::size_t k)
{
    if (k > n)
    {
        throw std::invalid_argument("sample_indices: k > n");
    }
    // A loop, not std::ranges::iota: older libc++ releases lack it.
    std::vector<std::size_t> indices(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        indices[i] = i;
    }
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto j = i + static_cast<std::size_t>(bounded(r, n - i));
        std::swap(indices[i], indices[j]);
    }
    indices.resize(k);
    return indices;
}

/// @brief A uniformly chosen element of a non-empty range.
///
/// `range[bounded(r, size)]`. Consumes one value (rarely more).
///
/// @param r Engine.
/// @param range Non-empty random-access range (an lvalue: the result refers
///        into it).
/// @return A reference to the chosen element.
/// @throws std::invalid_argument If @p range is empty.
template <std::ranges::random_access_range R>
    requires std::ranges::sized_range<R>
[[nodiscard]] constexpr decltype(auto) pick(rng& r, R& range)
{
    const auto n = static_cast<std::uint64_t>(std::ranges::size(range));
    if (n == 0)
    {
        throw std::invalid_argument("pick: empty range");
    }
    return std::ranges::begin(range)[static_cast<std::ptrdiff_t>(bounded(r, n))];
}

} // namespace dasmig::core


#endif // DASMIG_CORE_DISTRIBUTIONS_HPP
