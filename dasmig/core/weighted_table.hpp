// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_WEIGHTED_TABLE_HPP
#define DASMIG_CORE_WEIGHTED_TABLE_HPP

#include "distributions.hpp"
#include "rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/// @file weighted_table.hpp
/// @brief Weighted choice among values, with per-call conditional weights
///        and exclusions.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

namespace dasmig::core
{

/// @brief Values with weights, picked with probability proportional to the
///        weight.
///
/// A pick uses the same algorithm as discrete (cumulative sums in entry
/// order, one engine value), so entry order is part of the output.
///
/// Weights can be adjusted per call by a function of the entry and its base
/// weight, which is how context conditions a choice: return the base weight
/// to keep it, a multiple to favour it, or 0 to exclude it.
///
/// @code
/// dasmig::core::weighted_table<std::string> jobs{{"farmer", 3.0}, {"nurse", 2.0}, {"pilot", 0.2}};
/// const auto& job = jobs.pick(r);
/// const auto& adult_job = jobs.pick(r, [&](const std::string& j, double w) {
///     return (j == "pilot" && age < 21) ? 0.0 : w;
/// });
/// @endcode
///
/// @tparam T Value type.
template <typename T>
class weighted_table
{
  public:
    /// @brief A value and its base weight.
    struct entry
    {
        T value;       ///< The value.
        double weight; ///< Base weight, finite and >= 0.
    };

    /// @brief An empty table.
    weighted_table() = default;

    /// @brief A table from entries.
    /// @param entries Values and weights.
    /// @throws std::invalid_argument If a weight is negative or not finite.
    weighted_table(std::initializer_list<entry> entries)
    {
        for (const auto& e : entries) add(e.value, e.weight);
    }

    /// @brief Append an entry.
    /// @param value The value.
    /// @param weight Base weight, finite and >= 0.
    /// @return `*this`.
    /// @throws std::invalid_argument If @p weight is negative or not finite,
    ///         or the total weight overflows.
    weighted_table& add(T value, double weight)
    {
        check_weight(weight);
        const double total = (_cumulative.empty() ? 0.0 : _cumulative.back()) + weight;
        if (!std::isfinite(total))
        {
            throw std::invalid_argument("weighted_table: total weight overflows");
        }
        if (weight > 0.0) _last_positive = _entries.size();
        _entries.push_back({.value = std::move(value), .weight = weight});
        _cumulative.push_back(total);
        return *this;
    }

    /// @brief Number of entries.
    /// @return The size.
    [[nodiscard]] std::size_t size() const noexcept { return _entries.size(); }

    /// @brief Whether there are no entries.
    /// @return True when empty.
    [[nodiscard]] bool empty() const noexcept { return _entries.empty(); }

    /// @brief The entries, in order.
    /// @return The entries.
    [[nodiscard]] std::span<const entry> entries() const noexcept { return _entries; }

    /// @brief Sum of the base weights.
    /// @return The total.
    [[nodiscard]] double total_weight() const noexcept
    {
        return _cumulative.empty() ? 0.0 : _cumulative.back();
    }

    /// @brief Pick an index by base weight. Consumes one engine value.
    /// @param r Engine.
    /// @return The index of the chosen entry.
    /// @throws std::invalid_argument If no entry has a positive weight.
    [[nodiscard]] std::size_t pick_index(rng& r) const
    {
        if (!(total_weight() > 0.0))
        {
            throw std::invalid_argument("weighted_table: no entry has a positive weight");
        }
        return detail::pick_cumulative(r, _cumulative, _last_positive);
    }

    /// @brief Pick a value by base weight. Consumes one engine value.
    /// @param r Engine.
    /// @return The chosen value.
    /// @throws std::invalid_argument If no entry has a positive weight.
    [[nodiscard]] const T& pick(rng& r) const { return _entries[pick_index(r)].value; }

    /// @brief Pick an index with adjusted weights, or nothing when every
    ///        adjusted weight is 0. Always consumes one engine value.
    /// @tparam F Callable `double(const T& value, double base_weight)`.
    /// @param r Engine.
    /// @param adjust Returns the weight to use (0 excludes the entry).
    /// @return The index, or empty.
    /// @throws std::invalid_argument If @p adjust returns a negative or
    ///         non-finite weight, or the adjusted total overflows.
    template <std::invocable<const T&, double> F>
    [[nodiscard]] std::optional<std::size_t> try_pick_index(rng& r, const F& adjust) const
    {
        // The sums of a small table stay on the stack: an allocation would
        // cost more than the pick.
        std::array<double, inline_sums_> inline_sums; // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init): written before read
        std::vector<double> heap_sums;
        std::span<double> cumulative{inline_sums.data(), std::min(_entries.size(), inline_sums_)};
        if (_entries.size() > inline_sums_)
        {
            heap_sums.resize(_entries.size());
            cumulative = heap_sums;
        }
        std::size_t last_positive = 0;
        double total = 0.0;
        for (std::size_t i = 0; i < _entries.size(); ++i)
        {
            const auto w = static_cast<double>(adjust(_entries[i].value, _entries[i].weight));
            check_weight(w);
            total += w;
            if (w > 0.0) last_positive = i;
            cumulative[i] = total;
        }
        if (!std::isfinite(total))
        {
            throw std::invalid_argument("weighted_table: adjusted total weight overflows");
        }
        if (!(total > 0.0))
        {
            (void)r(); // keep the stream position independent of the outcome
            return std::nullopt;
        }
        return detail::pick_cumulative(r, cumulative, last_positive);
    }

    /// @brief Pick a value with adjusted weights, or nothing when every
    ///        adjusted weight is 0. Always consumes one engine value.
    /// @tparam F As try_pick_index().
    /// @param r Engine.
    /// @param adjust Returns the weight to use (0 excludes the entry).
    /// @return A pointer to the chosen value, or nullptr.
    /// @throws std::invalid_argument As try_pick_index().
    template <std::invocable<const T&, double> F>
    [[nodiscard]] const T* try_pick(rng& r, const F& adjust) const
    {
        const auto i = try_pick_index(r, adjust);
        return i ? &_entries[*i].value : nullptr;
    }

    /// @brief Pick a value with adjusted weights. Consumes one engine value.
    /// @tparam F As try_pick_index().
    /// @param r Engine.
    /// @param adjust Returns the weight to use (0 excludes the entry).
    /// @return The chosen value.
    /// @throws std::invalid_argument If every adjusted weight is 0, or as
    ///         try_pick_index().
    template <std::invocable<const T&, double> F>
    [[nodiscard]] const T& pick(rng& r, const F& adjust) const
    {
        const T* chosen = try_pick(r, adjust);
        if (chosen == nullptr)
        {
            throw std::invalid_argument("weighted_table: every adjusted weight is 0");
        }
        return *chosen;
    }

    /// @brief Pick up to @p k distinct indices, each draw by adjusted weight
    ///        among the entries not yet chosen.
    ///
    /// A draw where no remaining entry has a positive weight chooses nothing,
    /// so the result can be shorter than @p k. Consumes exactly @p k engine
    /// values either way.
    ///
    /// @tparam F As try_pick_index().
    /// @param r Engine.
    /// @param k Number of entries wanted.
    /// @param adjust Returns the weight to use (0 excludes the entry).
    /// @return The chosen indices, in the order drawn.
    /// @throws std::invalid_argument As try_pick_index().
    template <std::invocable<const T&, double> F>
    [[nodiscard]] std::vector<std::size_t> pick_distinct_indices(rng& r, std::size_t k,
                                                                 const F& adjust) const
    {
        std::vector<std::size_t> chosen;
        std::vector<bool> taken(_entries.size(), false);
        for (std::size_t n = 0; n < k; ++n)
        {
            std::size_t index = 0;
            const auto i = try_pick_index(r, [&](const T& value, double w) {
                const bool skip = taken[index++];
                return skip ? 0.0 : static_cast<double>(adjust(value, w));
            });
            if (!i) continue; // keep consuming one value per requested pick
            taken[*i] = true;
            chosen.push_back(*i);
        }
        return chosen;
    }

    /// @brief Pick up to @p k distinct indices by base weight.
    /// @param r Engine.
    /// @param k Number of entries wanted.
    /// @return The chosen indices, in the order drawn.
    [[nodiscard]] std::vector<std::size_t> pick_distinct_indices(rng& r, std::size_t k) const
    {
        return pick_distinct_indices(r, k, [](const T&, double w) { return w; });
    }

  private:
    static void check_weight(double w)
    {
        if (!std::isfinite(w) || w < 0.0)
        {
            throw std::invalid_argument("weighted_table: weights must be finite and >= 0");
        }
    }

    std::vector<entry> _entries;
    std::vector<double> _cumulative;

    // Entries whose adjusted sums try_pick_index() keeps on the stack.
    static constexpr std::size_t inline_sums_ = 64;
    std::size_t _last_positive{0};
};

} // namespace dasmig::core

#endif // DASMIG_CORE_WEIGHTED_TABLE_HPP
