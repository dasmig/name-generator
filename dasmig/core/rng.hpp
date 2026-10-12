#ifndef DASMIG_CORE_RNG_HPP
#define DASMIG_CORE_RNG_HPP

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <random>
#include <string_view>

/// @file rng.hpp
/// @brief The random engine every generator library uses: xoshiro256**
///        seeded through SplitMix64, with labelled child streams.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// The algorithms are fixed and the output is part of the core's contract:
/// a seed gives the same sequence on every compiler and platform. Changing
/// any of them is a major version change.

namespace dasmig::core
{

/// @brief The SplitMix64 output function (a bijection on 64-bit values).
/// @param z Input value.
/// @return The mixed value.
[[nodiscard]] constexpr std::uint64_t splitmix64_mix(std::uint64_t z) noexcept
{
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

/// @brief The SplitMix64 generator (Steele, Lea and Flood 2014).
///
/// Used to expand a 64-bit seed into the 256-bit xoshiro state, as the
/// xoshiro authors recommend.
class splitmix64
{
  public:
    /// @brief Construct from a seed.
    /// @param seed Initial state.
    constexpr explicit splitmix64(std::uint64_t seed) noexcept : _state{seed} {}

    /// @brief Advance and return the next value.
    /// @return The next 64-bit value.
    constexpr std::uint64_t operator()() noexcept
    {
        _state += 0x9e3779b97f4a7c15ULL;
        return splitmix64_mix(_state);
    }

  private:
    std::uint64_t _state;
};

/// @brief 64-bit FNV-1a hash of the bytes of a string.
///
/// Labels are hashed as bytes, so pass UTF-8: a `std::wstring` label would
/// hash differently on Windows (UTF-16) and elsewhere (UTF-32).
///
/// @param text Bytes to hash.
/// @return The hash.
[[nodiscard]] constexpr std::uint64_t fnv1a64(std::string_view text) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text)
    {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// @brief Combine a hash with another 64-bit value, order-sensitively.
///
/// `splitmix64_mix(hash ^ splitmix64_mix(value))`, the same construction as
/// rng::child_seed().
///
/// @param hash Running hash.
/// @param value Value to fold in.
/// @return The combined hash.
[[nodiscard]] constexpr std::uint64_t hash_combine(std::uint64_t hash,
                                                   std::uint64_t value) noexcept
{
    return splitmix64_mix(hash ^ splitmix64_mix(value));
}

/// @brief The core random engine: xoshiro256** (Blackman and Vigna 2018).
///
/// Satisfies `std::uniform_random_bit_generator`, but use the core's own
/// distributions with it: the `std::` distributions give different results
/// on different standard libraries.
///
/// An engine remembers the seed it was built from. split() derives a child
/// engine from that seed and a label, not from the current state, so the
/// streams a generator hands to its parts do not shift when another part
/// draws more or fewer numbers.
///
/// @code
/// dasmig::core::rng r{42};
/// auto height = r.split("height");   // same stream for seed 42, always
/// auto weight = r.split("weight");   // independent of height's draws
/// @endcode
class rng
{
  public:
    using result_type = std::uint64_t; ///< Type of the values produced.

    /// @brief Construct from a seed.
    /// @param seed Seed; every value, including 0, is a valid seed.
    constexpr explicit rng(std::uint64_t seed) noexcept { this->seed(seed); }

    /// @brief Construct with a seed taken from `std::random_device`.
    ///
    /// For the instance engines of non-seeded calls only; anything that must
    /// replay takes a seed explicitly.
    ///
    /// @return An engine with an unpredictable seed.
    [[nodiscard]] static rng from_entropy() { return rng{entropy_seed()}; }

    /// @brief A 64-bit seed from `std::random_device`.
    /// @return An unpredictable seed.
    [[nodiscard]] static std::uint64_t entropy_seed()
    {
        std::random_device device;
        const auto hi = static_cast<std::uint64_t>(device());
        const auto lo = static_cast<std::uint64_t>(device());
        return (hi << 32U) ^ lo;
    }

    /// @brief Reset the engine to the start of the sequence for a seed.
    /// @param seed The new seed.
    /// @return `*this`.
    constexpr rng& seed(std::uint64_t seed) noexcept
    {
        _seed = seed;
        splitmix64 expand{seed};
        for (auto& word : _s)
        {
            word = expand();
        }
        return *this;
    }

    /// @brief Reseed from `std::random_device`.
    /// @return `*this`.
    rng& unseed() { return seed(entropy_seed()); }

    /// @brief The seed this engine was last seeded with.
    /// @return The seed; constructing an engine from it replays the sequence.
    [[nodiscard]] constexpr std::uint64_t seed() const noexcept { return _seed; }

    /// @brief Advance and return the next value.
    /// @return A uniformly distributed 64-bit value.
    constexpr result_type operator()() noexcept
    {
        const std::uint64_t result = std::rotl(_s[1] * 5U, 7) * 9U;
        const std::uint64_t t = _s[1] << 17U;
        _s[2] ^= _s[0];
        _s[3] ^= _s[1];
        _s[1] ^= _s[2];
        _s[0] ^= _s[3];
        _s[2] ^= t;
        _s[3] = std::rotl(_s[3], 45);
        return result;
    }

    /// @brief Derive the child seed for a label.
    ///
    /// `splitmix64_mix(seed ^ splitmix64_mix(fnv1a64(label)))`.
    ///
    /// @param label UTF-8 label naming the child stream.
    /// @return The child seed.
    [[nodiscard]] constexpr std::uint64_t child_seed(
        std::string_view label) const noexcept
    {
        return splitmix64_mix(_seed ^ splitmix64_mix(fnv1a64(label)));
    }

    /// @brief Derive the child seed for an index.
    ///
    /// `splitmix64_mix(seed ^ splitmix64_mix(index ^ 0x6a09e667f3bcc909))`;
    /// the constant keeps index streams apart from label streams.
    ///
    /// @param index Index of the child stream (for example a family member).
    /// @return The child seed.
    [[nodiscard]] constexpr std::uint64_t child_seed(
        std::uint64_t index) const noexcept
    {
        return splitmix64_mix(
            _seed ^ splitmix64_mix(index ^ 0x6a09e667f3bcc909ULL));
    }

    /// @brief A child engine for a label, independent of this engine's state.
    /// @param label UTF-8 label naming the child stream.
    /// @return An engine seeded with child_seed(label).
    [[nodiscard]] constexpr rng split(std::string_view label) const noexcept
    {
        return rng{child_seed(label)};
    }

    /// @brief A child engine for an index, independent of this engine's state.
    /// @param index Index of the child stream.
    /// @return An engine seeded with child_seed(index).
    [[nodiscard]] constexpr rng split(std::uint64_t index) const noexcept
    {
        return rng{child_seed(index)};
    }

    /// @brief Smallest value produced.
    /// @return 0.
    [[nodiscard]] static constexpr result_type min() noexcept { return 0; }

    /// @brief Largest value produced.
    /// @return 2^64 - 1.
    [[nodiscard]] static constexpr result_type max() noexcept
    {
        return std::numeric_limits<result_type>::max();
    }

    /// @brief Engines are equal when their seed and position are equal.
    friend constexpr bool operator==(const rng&, const rng&) noexcept = default;

  private:
    std::array<std::uint64_t, 4> _s{};
    std::uint64_t _seed{0};
};

static_assert(std::uniform_random_bit_generator<rng>);

} // namespace dasmig::core

#endif // DASMIG_CORE_RNG_HPP
