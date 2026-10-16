// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_BATCH_HPP
#define DASMIG_CORE_BATCH_HPP

#include "rng.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

/// @file batch.hpp
/// @brief Batch generation: item seeds, parallel and streaming runs, and
///        uniqueness, shared by every generator library.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// A batch is `count` results generated from one batch seed. Item `i` is
/// exactly what the single-call getter returns for `item_seed(batch_seed, i)`,
/// so any item replays on its own, any range is computed directly, and the
/// result does not depend on how the work is divided between threads.

namespace dasmig::core
{

/// @brief The seed of item @p index in a batch.
///
/// `rng{batch_seed}.split(index).seed()`, computed without building an engine.
///
/// @param batch_seed Seed of the whole batch.
/// @param index Index of the item (including `batch_options::first`).
/// @return The seed to give the single-call getter for that item.
[[nodiscard]] constexpr std::uint64_t item_seed(std::uint64_t batch_seed,
                                                std::uint64_t index) noexcept
{
    return rng::child_seed_of(batch_seed, index);
}

/// @brief The seed of attempt @p attempt of item @p index, for a batch that
///        must be unique.
///
/// Attempt 0 is the plain item_seed(), so items that do not collide equal the
/// single call. Attempt `k > 0` is `item_seed(item_seed(batch_seed, index), k)`.
///
/// @param batch_seed Seed of the whole batch.
/// @param index Index of the item.
/// @param attempt Attempt number, starting at 0.
/// @return The seed to generate that attempt with.
[[nodiscard]] constexpr std::uint64_t attempt_seed(std::uint64_t batch_seed,
                                                   std::uint64_t index,
                                                   std::uint64_t attempt) noexcept
{
    const std::uint64_t base = item_seed(batch_seed, index);
    return attempt == 0 ? base : item_seed(base, attempt);
}

/// @brief Makes a batch unique by a key (an email, a username, ...).
///
/// Items are generated, then checked in index order; an item whose key was
/// already produced earlier in the same call is generated again with the next
/// attempt seed (see attempt_seed()) until it is new or the attempts run out.
/// The outcome is the same for any thread count. Uniqueness holds within one
/// call: two batches that are concatenated are not checked against each other.
class unique_rule
{
  public:
    /// @brief Unique by @p key.
    /// @tparam T The item type the batch produces.
    /// @tparam Key Callable taking `const T&` and returning something
    ///         convertible to `std::string`.
    /// @param key Extracts the key.
    /// @param max_attempts Attempts per item, including the first (at least 1).
    /// @return The rule.
    template <typename T, typename Key>
    [[nodiscard]] static unique_rule by(Key key, std::size_t max_attempts = 100)
    {
        unique_rule rule;
        rule._type = std::type_index{typeid(T)};
        rule._max_attempts = max_attempts;
        rule._key = [key = std::move(key)](const void* item)
        { return std::string{key(*static_cast<const T*>(item))}; };
        return rule;
    }

    /// @brief Attempts per item, including the first.
    /// @return The limit.
    [[nodiscard]] std::size_t max_attempts() const noexcept { return _max_attempts; }

    /// @brief The item type the rule was made for.
    /// @return Its type index.
    [[nodiscard]] std::type_index type() const noexcept { return _type; }

    /// @brief The key of an item.
    /// @param item Pointer to an item of the rule's type.
    /// @return The key.
    [[nodiscard]] std::string key_of(const void* item) const { return _key(item); }

  private:
    unique_rule() = default;

    std::type_index _type{typeid(void)};
    std::size_t _max_attempts{1};
    std::function<std::string(const void*)> _key;
};

/// @brief How a batch is generated. Requests keep describing one item.
struct batch_options
{
    std::uint64_t count{1};   ///< Number of items.
    std::uint64_t first{0};   ///< Index of the first item; item `i` is item_seed(seed, first + i).
    std::size_t threads{1};   ///< Worker threads; 0 means the hardware count, 1 runs on the caller.
    std::optional<unique_rule> unique{}; // NOLINT(readability-redundant-member-init): keeps -Wextra quiet for partial designated initializers
    ///< Make the batch unique by a key.
};

namespace detail
{

template <typename Make>
using batch_item_t = std::remove_cvref_t<std::invoke_result_t<const Make&, std::uint64_t>>;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline std::size_t resolve_threads(std::size_t requested, std::size_t work) noexcept
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    (void)requested;
    (void)work;
    return 1;
#else
    std::size_t threads = requested;
    if (threads == 0)
    {
        threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
    return std::max<std::size_t>(1, std::min(threads, work));
#endif
}

// Fills slots[0, n) with make(item_seed(batch_seed, first + k)), in contiguous
// chunks, one per thread. The failure of the lowest index is rethrown.
template <typename Make, typename Item>
void fill_window(const Make& make, std::uint64_t batch_seed, std::uint64_t first,
                 std::size_t n, std::size_t threads, std::vector<std::optional<Item>>& slots)
{
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const auto run_chunk = [&](std::size_t lo, std::size_t hi, std::exception_ptr& error)
    {
        try
        {
            for (std::size_t k = lo; k < hi; ++k)
            {
                slots[k].emplace(make(item_seed(batch_seed, first + k)));
            }
        }
        catch (...)
        {
            error = std::current_exception();
        }
    };

    if (threads <= 1)
    {
        std::exception_ptr error;
        run_chunk(0, n, error);
        if (error) std::rethrow_exception(error);
        return;
    }

    std::vector<std::exception_ptr> errors(threads);
    const auto bound = [&](std::size_t chunk) { return n * chunk / threads; };
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (std::size_t c = 1; c < threads; ++c)
    {
        try
        {
            workers.emplace_back(run_chunk, bound(c), bound(c + 1), std::ref(errors[c]));
        }
        catch (const std::system_error&)
        {
            // No thread available: the caller does this chunk after its own.
            run_chunk(bound(c), bound(c + 1), errors[c]);
        }
    }
    run_chunk(bound(0), bound(1), errors[0]);
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors)
    {
        if (error) std::rethrow_exception(error);
    }
}

template <typename Make, typename Sink>
void run_batch_windows(const batch_options& options, std::uint64_t batch_seed,
                       const Make& make, Sink& sink, std::uint64_t window_items)
{
    using item = batch_item_t<Make>;

    if (options.count > std::numeric_limits<std::uint64_t>::max() - options.first)
    {
        throw std::invalid_argument("batch: first + count overflows the item index");
    }
    if (options.unique)
    {
        if (options.unique->type() != std::type_index{typeid(item)})
        {
            throw std::invalid_argument("batch: the unique rule was made for another item type");
        }
        if (options.unique->max_attempts() == 0)
        {
            throw std::invalid_argument("batch: a unique rule needs at least one attempt");
        }
    }

    const std::size_t threads = resolve_threads(
        options.threads,
        static_cast<std::size_t>(std::min<std::uint64_t>(options.count, std::numeric_limits<std::size_t>::max())));
    std::unordered_set<std::string> seen;
    std::vector<std::optional<item>> slots;

    for (std::uint64_t done = 0; done < options.count;)
    {
        const auto n = static_cast<std::size_t>(std::min(window_items, options.count - done));
        const std::uint64_t window_first = options.first + done;
        slots.clear();
        slots.resize(n);
        fill_window(make, batch_seed, window_first, n, threads, slots);

        for (std::size_t k = 0; k < n; ++k)
        {
            const std::uint64_t index = window_first + k;
            if (options.unique)
            {
                const unique_rule& rule = *options.unique;
                std::string key = rule.key_of(&*slots[k]);
                for (std::uint64_t attempt = 1; !seen.insert(key).second; ++attempt)
                {
                    if (attempt >= rule.max_attempts())
                    {
                        throw std::runtime_error(
                            "batch: item " + std::to_string(index) + " has no unique key after " +
                            std::to_string(rule.max_attempts()) + " attempts");
                    }
                    slots[k].emplace(make(attempt_seed(batch_seed, index, attempt)));
                    key = rule.key_of(&*slots[k]);
                }
            }
            sink(index, *slots[k]);
        }
        done += n;
    }
}

} // namespace detail

/// @brief Generate a batch and return it.
///
/// Item `i` is `make(item_seed(batch_seed, options.first + i))`. @p make runs
/// on several threads when `options.threads` allows, so it must be callable
/// concurrently (the libraries' const seeded getters are).
///
/// @code
/// auto names = dasmig::core::run_batch({.count = 1000, .threads = 4}, 7,
///     [&](std::uint64_t seed) { return gen.get_name(request, seed); });
/// @endcode
///
/// @tparam Make Callable taking a `std::uint64_t` seed and returning an item.
/// @param options Count, first index, threads and uniqueness.
/// @param batch_seed Seed of the whole batch.
/// @param make Generates one item from its seed.
/// @return The items, in index order.
/// @throws std::invalid_argument If the options are inconsistent.
/// @throws std::runtime_error If a unique batch runs out of attempts.
/// @throws Whatever @p make throws, for the lowest failing index.
template <typename Make>
    requires std::invocable<const Make&, std::uint64_t>
[[nodiscard]] std::vector<detail::batch_item_t<Make>> run_batch(const batch_options& options,
                                                               std::uint64_t batch_seed,
                                                               const Make& make)
{
    using item = detail::batch_item_t<Make>;
    std::vector<item> out;
    out.reserve(static_cast<std::size_t>(options.count));
    auto sink = [&out](std::uint64_t, item& value) { out.push_back(std::move(value)); };
    detail::run_batch_windows(options, batch_seed, make, sink,
                              std::max<std::uint64_t>(options.count, 1));
    return out;
}

/// @brief Generate a batch and pass each item to @p sink, in index order,
///        without holding the whole batch.
///
/// At most a few thousand items per thread are alive at once (plus the keys of
/// a unique batch). @p sink runs on the calling thread and receives the
/// item's index (including `options.first`) and the item.
///
/// @tparam Make Callable taking a `std::uint64_t` seed and returning an item.
/// @tparam Sink Callable taking `(std::uint64_t index, Item&)`.
/// @param options Count, first index, threads and uniqueness.
/// @param batch_seed Seed of the whole batch.
/// @param make Generates one item from its seed.
/// @param sink Receives the items.
/// @throws The same errors as run_batch(), and whatever @p sink throws.
template <typename Make, typename Sink>
    requires std::invocable<const Make&, std::uint64_t>
void for_each_batch(const batch_options& options, std::uint64_t batch_seed, const Make& make,
                    Sink&& sink) // NOLINT(cppcoreguidelines-missing-std-forward): called as an lvalue, never moved
{
    const std::size_t threads = detail::resolve_threads(options.threads, std::numeric_limits<std::size_t>::max());
    detail::run_batch_windows(options, batch_seed, make, sink,
                              static_cast<std::uint64_t>(threads) * 4096U);
}

} // namespace dasmig::core

#endif // DASMIG_CORE_BATCH_HPP
