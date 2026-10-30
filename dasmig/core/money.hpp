// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_MONEY_HPP
#define DASMIG_CORE_MONEY_HPP

#include "json.hpp"
#include "math.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

/// @file money.hpp
/// @brief An amount in integer minor units with its ISO 4217 currency.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// The core knows no currencies: the minor unit (2 for BRL, 0 for JPY, 3 for
/// KWD) comes from the caller, typically country-generator's
/// `currency_digits`. There is no arithmetic beyond equality and no
/// conversion; locale formatting (grouping, symbols) is country-generator's
/// `format_money`.

namespace dasmig::core
{

/// @brief An amount of money: integer minor units, a currency and its minor unit.
///
/// Make one with from_minor() or from_major(), which check the invariants:
/// a code of three capital letters, digits from 0 to 4, and |minor| at most
/// 2^53, so minor converts to a double exactly and major() is the closest
/// double to the amount. Up to 2^51 minor units, the JSON number times
/// 10^digits rounds back to minor exactly.
///
/// @code
/// auto income = core::money::from_major(core::lognormal(r, 10.4, 0.6), "BRL", 2);
/// income.decimal();         // for example "32861.07"
/// income.write_json(w);     // {"amount":32861.07,"currency":"BRL"}
/// core::money::from_minor(-5, "USD", 2).decimal();   // "-0.05"
/// @endcode
struct money
{
    std::int64_t minor{};   ///< Amount in minor units (cents); negative allowed.
    std::string currency;   ///< ISO 4217 alphabetic code: three capital letters.
    std::uint8_t digits{2}; ///< ISO 4217 minor unit, 0 to 4.

    /// @brief Largest |minor| accepted: 2^53.
    static constexpr std::int64_t max_minor = 9007199254740992;

    /// @brief An amount from minor units.
    /// @param minor Amount in minor units.
    /// @param currency ISO 4217 alphabetic code.
    /// @param digits The currency's minor unit, 0 to 4.
    /// @return The amount.
    /// @throws std::invalid_argument For a code that is not three capital
    ///         letters, digits above 4, or |minor| above 2^53.
    [[nodiscard]] static money from_minor(std::int64_t minor, std::string currency,
                                          std::uint8_t digits)
    {
        check(currency, digits, "money::from_minor");
        if (minor > max_minor || minor < -max_minor)
        {
            throw std::invalid_argument("money::from_minor: " + std::to_string(minor) +
                                        " minor units is beyond 2^53");
        }
        return money{.minor = minor, .currency = std::move(currency), .digits = digits};
    }

    /// @brief An amount from major units, rounded to the minor unit.
    ///
    /// `y = |amount| * 10^digits` (one correctly rounded multiplication by an
    /// exact power of ten), `f = floor(y)`, plus 1 when `y - f >= 0.5`
    /// (halves away from zero), with the sign put back. 1.005 gives 100 cents,
    /// because the double 1.005 is just below 1.005; draw minor units with
    /// from_minor() when an exact decimal amount matters.
    ///
    /// @param amount Amount in major units.
    /// @param currency ISO 4217 alphabetic code.
    /// @param digits The currency's minor unit, 0 to 4.
    /// @return The amount.
    /// @throws std::invalid_argument As from_minor(), or if @p amount is NaN
    ///         or infinite.
    [[nodiscard]] static money from_major(double amount, std::string currency,
                                          std::uint8_t digits)
    {
        check(currency, digits, "money::from_major");
        if (!std::isfinite(amount))
        {
            throw std::invalid_argument("money::from_major: amount is not finite");
        }
        const double y = math::fenced(std::abs(amount) * pow10(digits));
        if (y > static_cast<double>(max_minor))
        {
            throw std::invalid_argument("money::from_major: amount is beyond 2^53 minor units");
        }
        double f = std::floor(y);
        if (y - f >= 0.5) f += 1.0; // never past 2^53: doubles that large are integers
        const auto units = static_cast<std::int64_t>(f);
        return money{.minor = amount < 0.0 ? -units : units,
                     .currency = std::move(currency),
                     .digits = digits};
    }

    /// @brief The amount in major units.
    /// @return `minor / 10^digits` (one correctly rounded division).
    [[nodiscard]] double major() const noexcept
    {
        return static_cast<double>(minor) / pow10(digits);
    }

    /// @brief The exact amount as decimal text.
    ///
    /// Integer arithmetic: the sign, the major units, and when digits > 0 a
    /// `.` and the minor units padded to @c digits characters. No grouping
    /// and no locale.
    ///
    /// @return For example `"1234.50"`, `"-0.05"`, `"1000"`, `"0.125"`.
    [[nodiscard]] std::string decimal() const
    {
        const auto magnitude = minor < 0 ? 0 - static_cast<std::uint64_t>(minor)
                                         : static_cast<std::uint64_t>(minor);
        std::uint64_t scale = 1;
        for (std::uint8_t i = 0; i < digits; ++i) scale *= 10;
        std::string text = minor < 0 ? "-" : "";
        text += std::to_string(magnitude / scale);
        if (digits > 0)
        {
            const std::string fraction = std::to_string(magnitude % scale);
            text += '.';
            text.append(digits - fraction.size(), '0');
            text += fraction;
        }
        return text;
    }

    /// @brief Write the amount as a JSON object.
    ///
    /// `{"amount":1234.5,"currency":"BRL"}`: the amount is major() written
    /// as json_writer writes a double, the shortest text that reads back to
    /// the same value. Write the key first, as for any value.
    ///
    /// @param w Writer, where a value is expected.
    void write_json(json_writer& w) const
    {
        w.begin_object().field("amount", major()).field("currency", currency).end_object();
    }

    /// @brief Equal when the minor units, the currency and the digits are.
    friend bool operator==(const money&, const money&) = default;

  private:
    static void check(const std::string& currency, std::uint8_t digits, const char* function)
    {
        if (currency.size() != 3 || currency[0] < 'A' || currency[0] > 'Z' || currency[1] < 'A' ||
            currency[1] > 'Z' || currency[2] < 'A' || currency[2] > 'Z')
        {
            throw std::invalid_argument(std::string{function} + ": currency '" + currency +
                                        "' is not three capital letters");
        }
        if (digits > 4)
        {
            throw std::invalid_argument(std::string{function} + ": " + std::to_string(digits) +
                                        " minor-unit digits, above 4");
        }
    }

    // 10^n, exact for n up to 22.
    [[nodiscard]] static constexpr double pow10(std::uint8_t n) noexcept
    {
        double p = 1.0;
        for (std::uint8_t i = 0; i < n; ++i) p *= 10.0;
        return p;
    }
};

} // namespace dasmig::core

#endif // DASMIG_CORE_MONEY_HPP
