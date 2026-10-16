// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_SEED_SIGNATURE_HPP
#define DASMIG_CORE_SEED_SIGNATURE_HPP

#include "json.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

/// @file seed_signature.hpp
/// @brief The replay format of every generated result.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

namespace dasmig::core
{

/// @brief What a result needs to be generated again: its call seed and the
///        data it was generated from.
///
/// `data` is the layered_table::data_hash() (or a combination of several)
/// of the data loaded when the result was made; 0 means "not recorded". A
/// seed replays the same result only against the same data, so a replay can
/// compare `data` and report a mismatch instead of returning a different
/// result silently.
///
/// The text form is `<seed>-<data>`, each 16 lowercase hex digits, for
/// example `00000000000004d2-9ae16a3b2f90404f`. In JSON both parts are
/// strings, because JavaScript numbers cannot hold every 64-bit value.
struct seed_signature
{
    std::uint64_t seed{0}; ///< The call seed.
    std::uint64_t data{0}; ///< Hash of the data used; 0 when not recorded.

    /// @brief The text form.
    /// @return `<seed>-<data>` in 16-digit lowercase hex.
    [[nodiscard]] std::string to_string() const
    {
        return hex(seed) + "-" + hex(data);
    }

    /// @brief Parse the text form. A lone seed (16 hex digits) is accepted
    ///        with data 0.
    /// @param text `<seed>-<data>` or `<seed>`.
    /// @return The signature.
    /// @throws std::invalid_argument If @p text is not in that form.
    [[nodiscard]] static seed_signature parse(std::string_view text)
    {
        const auto dash = text.find('-');
        const auto seed = parse_hex(text.substr(0, dash));
        const auto data = dash == std::string_view::npos
                              ? std::optional<std::uint64_t>{0}
                              : parse_hex(text.substr(dash + 1));
        if (!seed || !data)
        {
            throw std::invalid_argument("seed_signature: expected 16 hex digits, "
                                        "optionally followed by '-' and 16 more");
        }
        return {.seed = *seed, .data = *data};
    }

    /// @brief Write as a JSON object `{"seed":"…","data":"…"}`.
    /// @param w Writer positioned where a value is allowed.
    void to_json(json_writer& w) const
    {
        w.begin_object().field("seed", hex(seed)).field("data", hex(data)).end_object();
    }

    /// @brief Signatures are equal when seed and data are equal.
    friend constexpr bool operator==(const seed_signature&, const seed_signature&) noexcept = default;

  private:
    static std::string hex(std::uint64_t v)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out(16, '0');
        for (std::size_t i = 16; i-- > 0;)
        {
            out[i] = digits[v & 0xFU];
            v >>= 4U;
        }
        return out;
    }

    static std::optional<std::uint64_t> parse_hex(std::string_view text)
    {
        if (text.size() != 16) return std::nullopt;
        for (const char c : text)
        {
            const bool digit = c >= '0' && c <= '9';
            const bool lower_hex = c >= 'a' && c <= 'f';
            if (!digit && !lower_hex) return std::nullopt;
        }
        std::uint64_t v = 0;
        const auto res = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), v, 16);
        if (res.ec != std::errc{}) return std::nullopt;
        return v;
    }
};

} // namespace dasmig::core

#endif // DASMIG_CORE_SEED_SIGNATURE_HPP
