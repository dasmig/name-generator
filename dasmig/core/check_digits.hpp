// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_CHECK_DIGITS_HPP
#define DASMIG_CORE_CHECK_DIGITS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

/// @file check_digits.hpp
/// @brief Check-digit schemes used by identifiers: Luhn, ISO 7064 (MOD 97-10,
///        MOD 11-2, MOD 11,10), IBAN, weighted sums, GTIN, ICAO 9303 and
///        Verhoeff.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// Inputs are the electronic form: no spaces, dashes or dots. A library that
/// shows a grouped form groups after computing. The `*_digit` functions throw
/// `std::invalid_argument` for an empty or malformed body, naming the function
/// and the 0-based position (`luhn_digit: position 3: '-' is not a digit`);
/// the `*_valid` functions never throw, and malformed input is simply not
/// valid. Nothing here allocates except to build an error message, and every
/// function can run at compile time.
///
/// The national mod-11 rules (Brazil's CPF, the UK's NHS number and others)
/// reduce a weighted sum each in their own way, so they stay in the libraries,
/// built on weighted_sum().

namespace dasmig::core
{

/// @cond INTERNAL
namespace detail
{

[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool is_capital(char c) noexcept { return c >= 'A' && c <= 'Z'; }
[[nodiscard]] constexpr bool is_digit_or_capital(char c) noexcept
{
    return is_digit(c) || is_capital(c);
}

[[nodiscard]] constexpr bool all_digits(std::string_view s) noexcept
{
    return std::ranges::all_of(s, is_digit);
}

[[nodiscard]] constexpr bool all_digits_or_capitals(std::string_view s) noexcept
{
    return std::ranges::all_of(s, is_digit_or_capital);
}

// "'-'" for a printable ASCII character, "byte 0x80" for anything else.
[[nodiscard]] inline std::string describe_char(char c)
{
    const auto b = static_cast<std::uint8_t>(c);
    if (b >= 0x20U && b <= 0x7EU) return std::string{'\''} + c + '\'';
    constexpr std::string_view hex = "0123456789ABCDEF";
    return std::string{"byte 0x"} + hex[b >> 4U] + hex[b & 0x0FU];
}

// "<function>: position <i>: <char> is not <expected>".
[[noreturn]] inline void throw_bad_char(std::string_view function, std::size_t position,
                                        char c, std::string_view expected)
{
    throw std::invalid_argument(std::string{function} + ": position " +
                                std::to_string(position) + ": " + describe_char(c) +
                                " is not " + std::string{expected});
}

[[noreturn]] inline void throw_empty(std::string_view function)
{
    throw std::invalid_argument(std::string{function} + ": empty input");
}

[[noreturn]] inline void throw_at(std::string_view function, std::size_t position,
                                  const std::exception& e)
{
    throw std::invalid_argument(std::string{function} + ": position " +
                                std::to_string(position) + ": " + e.what());
}

// The digit at s[i], or an error naming the function and the position.
[[nodiscard]] constexpr unsigned digit_at(std::string_view function, std::string_view s, // NOLINT(bugprone-easily-swappable-parameters)
                                          std::size_t i)
{
    if (!is_digit(s[i])) throw_bad_char(function, i, s[i], "a digit");
    return static_cast<unsigned>(s[i] - '0');
}

// '0'-'9' as 0-9 and 'A'-'Z' as 10-35 at s[i], or an error.
[[nodiscard]] constexpr unsigned alnum_at(std::string_view function, std::string_view s, // NOLINT(bugprone-easily-swappable-parameters)
                                          std::size_t i)
{
    const char c = s[i];
    if (is_digit(c)) return static_cast<unsigned>(c - '0');
    if (is_capital(c)) return static_cast<unsigned>(c - 'A') + 10U;
    throw_bad_char(function, i, c, "a digit or capital letter");
}

// One character value into an ISO 7064 MOD 97-10 remainder: a value of 10 or
// more is two decimal digits, as ISO 13616 writes letters.
[[nodiscard]] constexpr unsigned mod97_feed(unsigned r, unsigned value) noexcept
{
    if (value >= 10U) r = ((r * 10U) + (value / 10U)) % 97U;
    return ((r * 10U) + (value % 10U)) % 97U;
}

// The remainder after feeding every character of s (digits and capitals only).
[[nodiscard]] constexpr unsigned mod97_run(unsigned r, std::string_view function,
                                           std::string_view s)
{
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        r = mod97_feed(r, alnum_at(function, s, i));
    }
    return r;
}

[[nodiscard]] constexpr std::array<char, 2> mod97_check(unsigned r) noexcept
{
    r = (r * 100U) % 97U; // the two placeholder check digits, both 0
    const unsigned c = 98U - r;
    return {static_cast<char>('0' + (c / 10U)), static_cast<char>('0' + (c % 10U))};
}

// Verhoeff (1969): multiplication in the dihedral group D5, the position
// permutation and the inverse.
inline constexpr std::array<std::array<std::uint8_t, 10>, 10> verhoeff_d{{
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {1, 2, 3, 4, 0, 6, 7, 8, 9, 5},
    {2, 3, 4, 0, 1, 7, 8, 9, 5, 6},
    {3, 4, 0, 1, 2, 8, 9, 5, 6, 7},
    {4, 0, 1, 2, 3, 9, 5, 6, 7, 8},
    {5, 9, 8, 7, 6, 0, 4, 3, 2, 1},
    {6, 5, 9, 8, 7, 1, 0, 4, 3, 2},
    {7, 6, 5, 9, 8, 2, 1, 0, 4, 3},
    {8, 7, 6, 5, 9, 3, 2, 1, 0, 4},
    {9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
}};
inline constexpr std::array<std::array<std::uint8_t, 10>, 8> verhoeff_p{{
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {1, 5, 7, 6, 2, 8, 3, 0, 9, 4},
    {5, 8, 0, 3, 7, 9, 6, 1, 4, 2},
    {8, 9, 1, 6, 0, 4, 3, 5, 2, 7},
    {9, 4, 5, 3, 1, 2, 6, 8, 7, 0},
    {4, 2, 8, 6, 5, 7, 3, 9, 0, 1},
    {2, 7, 9, 3, 8, 0, 6, 4, 1, 5},
    {7, 0, 4, 6, 9, 1, 3, 2, 5, 8},
}};
inline constexpr std::array<std::uint8_t, 10> verhoeff_inv{0, 4, 3, 2, 1, 5, 6, 7, 8, 9};

} // namespace detail
/// @endcond

// --- Character values ---------------------------------------------------------

/// @brief The value of a decimal digit.
/// @param c `'0'` to `'9'`.
/// @return 0 to 9.
/// @throws std::invalid_argument For any other character.
[[nodiscard]] constexpr unsigned digit_value(char c)
{
    if (!detail::is_digit(c))
    {
        throw std::invalid_argument("digit_value: " + detail::describe_char(c) +
                                    " is not a digit");
    }
    return static_cast<unsigned>(c - '0');
}

/// @brief The value of a digit or capital letter, as ISO 7064 counts them.
/// @param c `'0'` to `'9'` (0 to 9) or `'A'` to `'Z'` (10 to 35).
/// @return 0 to 35.
/// @throws std::invalid_argument For any other character, lower case included.
[[nodiscard]] constexpr unsigned alnum_value(char c)
{
    if (detail::is_digit(c)) return static_cast<unsigned>(c - '0');
    if (detail::is_capital(c)) return static_cast<unsigned>(c - 'A') + 10U;
    throw std::invalid_argument("alnum_value: " + detail::describe_char(c) +
                                " is not a digit or capital letter");
}

// --- Luhn (ISO/IEC 7812-1 Annex B) ----------------------------------------------

/// @brief The Luhn check digit of a body of digits.
///
/// Over the digits from the right, with i = 0 for the rightmost: the digit,
/// doubled when i is even, minus 9 when the double exceeds 9, summed. The
/// check digit is `(10 - sum mod 10) mod 10`.
///
/// @param body Digits, without the check digit.
/// @return `'0'` to `'9'`.
/// @throws std::invalid_argument If @p body is empty or holds a non-digit.
[[nodiscard]] constexpr char luhn_digit(std::string_view body)
{
    if (body.empty()) detail::throw_empty("luhn_digit");
    unsigned sum = 0;
    for (std::size_t k = 0; k < body.size(); ++k)
    {
        unsigned v = detail::digit_at("luhn_digit", body, body.size() - 1 - k);
        if (k % 2 == 0)
        {
            v *= 2;
            if (v > 9) v -= 9;
        }
        sum += v;
    }
    return static_cast<char>('0' + ((10U - (sum % 10U)) % 10U));
}

/// @brief Whether a value ends in its Luhn check digit.
/// @param full Body and check digit.
/// @return True for at least two digits whose last is the body's Luhn digit.
[[nodiscard]] constexpr bool luhn_valid(std::string_view full) noexcept
{
    if (full.size() < 2 || !detail::all_digits(full)) return false;
    return luhn_digit(full.substr(0, full.size() - 1)) == full.back();
}

// --- ISO 7064 MOD 97-10 and IBAN (ISO 13616) ----------------------------------------

/// @brief The two ISO 7064 MOD 97-10 check digits of a body.
///
/// Each character's value (digits as themselves, A to Z as 10 to 35) is
/// written in decimal and fed into `r = (r * 10 + digit) mod 97`, then two
/// placeholder zeros; the check digits are `98 - r`, from `02` to `98`.
///
/// @param body Digits and capital letters, without the check digits.
/// @return The two check digits.
/// @throws std::invalid_argument If @p body is empty or holds another character.
[[nodiscard]] constexpr std::array<char, 2> mod97_10_digits(std::string_view body)
{
    if (body.empty()) detail::throw_empty("mod97_10_digits");
    return detail::mod97_check(detail::mod97_run(0, "mod97_10_digits", body));
}

/// @brief Whether a value ends in its two ISO 7064 MOD 97-10 check digits.
/// @param full Body followed by the two check digits.
/// @return True when the whole value, fed as in mod97_10_digits(), leaves 1.
[[nodiscard]] constexpr bool mod97_10_valid(std::string_view full) noexcept
{
    if (full.size() < 3 || !detail::all_digits_or_capitals(full) ||
        !detail::all_digits(full.substr(full.size() - 2)))
    {
        return false;
    }
    return detail::mod97_run(0, "mod97_10_valid", full) == 1;
}

/// @brief The two IBAN check digits for a country and a BBAN.
///
/// `mod97_10_digits(bban + country)`: ISO 13616 moves the country code and
/// the check digits to the end, and the check digits are the placeholders.
///
/// @param country ISO 3166-1 alpha-2 code, two capital letters.
/// @param bban Basic bank account number: digits and capital letters.
/// @return The two check digits that follow the country code.
/// @throws std::invalid_argument If @p country is not two capital letters, or
///         @p bban is empty or holds another character.
[[nodiscard]] constexpr std::array<char, 2> iban_check_digits(std::string_view country, // NOLINT(bugprone-easily-swappable-parameters)
                                                              std::string_view bban)
{
    if (country.size() != 2)
    {
        throw std::invalid_argument("iban_check_digits: country must be two capital letters");
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        if (!detail::is_capital(country[i]))
        {
            detail::throw_bad_char("iban_check_digits: country", i, country[i], "a capital letter");
        }
    }
    if (bban.empty()) detail::throw_empty("iban_check_digits");
    const unsigned r = detail::mod97_run(0, "iban_check_digits: bban", bban);
    return detail::mod97_check(detail::mod97_run(r, "iban_check_digits: country", country));
}

/// @brief Whether an IBAN's check digits are right.
///
/// Checks the electronic form (no spaces): 15 to 34 characters, two capital
/// letters, two digits, then digits and capital letters, and MOD 97-10 over
/// the BBAN followed by the first four characters. The length each country
/// requires is data, and is not checked here.
///
/// @param iban The IBAN.
/// @return True when the shape and the check digits are right.
[[nodiscard]] constexpr bool iban_valid(std::string_view iban) noexcept
{
    if (iban.size() < 15 || iban.size() > 34 || !detail::is_capital(iban[0]) ||
        !detail::is_capital(iban[1]) || !detail::is_digit(iban[2]) ||
        !detail::is_digit(iban[3]) || !detail::all_digits_or_capitals(iban.substr(4)))
    {
        return false;
    }
    const unsigned r = detail::mod97_run(0, "iban_valid", iban.substr(4));
    return detail::mod97_run(r, "iban_valid", iban.substr(0, 4)) == 1;
}

// --- ISO 7064 MOD 11-2 and MOD 11,10 ----------------------------------------------

/// @brief The ISO 7064 MOD 11-2 check character of a body of digits.
///
/// The pure system: `p = ((p + digit) * 2) mod 11` from p = 0, then
/// `c = (12 - p) mod 11`, written `X` when 10.
///
/// @param body Digits, without the check character.
/// @return `'0'` to `'9'` or `'X'`.
/// @throws std::invalid_argument If @p body is empty or holds a non-digit.
[[nodiscard]] constexpr char mod11_2_digit(std::string_view body)
{
    if (body.empty()) detail::throw_empty("mod11_2_digit");
    unsigned p = 0;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        p = ((p + detail::digit_at("mod11_2_digit", body, i)) * 2U) % 11U;
    }
    const unsigned c = (12U - p) % 11U;
    return c == 10U ? 'X' : static_cast<char>('0' + c);
}

/// @brief Whether a value ends in its ISO 7064 MOD 11-2 check character.
/// @param full Digits followed by the check character (a digit or `X`).
/// @return True when the check character is right.
[[nodiscard]] constexpr bool mod11_2_valid(std::string_view full) noexcept
{
    if (full.size() < 2) return false;
    const std::string_view body = full.substr(0, full.size() - 1);
    if (!detail::all_digits(body) || (!detail::is_digit(full.back()) && full.back() != 'X'))
    {
        return false;
    }
    return mod11_2_digit(body) == full.back();
}

/// @brief The ISO 7064 MOD 11,10 check digit of a body of digits.
///
/// The hybrid system: from p = 10, for each digit `s = (p + digit) mod 10`
/// (10 when 0) and `p = (2 * s) mod 11`; the check digit is `(11 - p) mod 10`.
///
/// @param body Digits, without the check digit.
/// @return `'0'` to `'9'`.
/// @throws std::invalid_argument If @p body is empty or holds a non-digit.
[[nodiscard]] constexpr char mod11_10_digit(std::string_view body)
{
    if (body.empty()) detail::throw_empty("mod11_10_digit");
    unsigned p = 10;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        unsigned s = (p + detail::digit_at("mod11_10_digit", body, i)) % 10U;
        if (s == 0) s = 10;
        p = (2U * s) % 11U;
    }
    return static_cast<char>('0' + ((11U - p) % 10U));
}

/// @brief Whether a value ends in its ISO 7064 MOD 11,10 check digit.
/// @param full Body and check digit.
/// @return True when the check digit is right.
[[nodiscard]] constexpr bool mod11_10_valid(std::string_view full) noexcept
{
    if (full.size() < 2 || !detail::all_digits(full)) return false;
    return mod11_10_digit(full.substr(0, full.size() - 1)) == full.back();
}

// --- Weighted sums ------------------------------------------------------------------

/// @brief The weighted sum the national mod-11 rules (and others) start from.
///
/// `sum of value_of(body[i]) * weights[i mod weights.size()]`, left to right,
/// in 64-bit unsigned arithmetic. The caller reduces it and maps the
/// remainder, as its rule says.
///
/// @code
/// // Brazil's CPF: weights 10 to 2, digit 0 when the remainder is below 2.
/// constexpr std::array<unsigned, 9> w{10, 9, 8, 7, 6, 5, 4, 3, 2};
/// const auto r = core::weighted_sum("111444777", w) % 11;   // 8
/// const char first = static_cast<char>('0' + (r < 2 ? 0 : 11 - r));   // '3'
/// @endcode
///
/// @tparam ValueOf Callable from char to an unsigned value.
/// @param body Characters to sum; may be empty (the sum is then 0).
/// @param weights Weights, repeated from the left when shorter than @p body.
/// @param value_of Character value; digit_value() by default.
/// @return The sum.
/// @throws std::invalid_argument If @p weights is empty, or @p value_of throws
///         it for a character (the message then names the position).
template <class ValueOf = decltype(&digit_value)>
[[nodiscard]] constexpr std::uint64_t weighted_sum(std::string_view body,
                                                   std::span<const unsigned> weights,
                                                   ValueOf value_of = &digit_value)
{
    if (weights.empty())
    {
        throw std::invalid_argument("weighted_sum: no weights");
    }
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        std::uint64_t v = 0;
        try
        {
            v = value_of(body[i]);
        }
        catch (const std::invalid_argument& e)
        {
            detail::throw_at("weighted_sum", i, e);
        }
        sum += v * weights[i % weights.size()];
    }
    return sum;
}

// --- GTIN (GS1) -----------------------------------------------------------------------

/// @brief The GS1 check digit (GTIN-8, -12, -13, -14, SSCC, ISBN-13).
///
/// From the right, with i = 0 for the rightmost body digit: weight 3 when i is
/// even, else 1. The check digit is `(10 - sum mod 10) mod 10`. Any length.
///
/// @param body Digits, without the check digit.
/// @return `'0'` to `'9'`.
/// @throws std::invalid_argument If @p body is empty or holds a non-digit.
[[nodiscard]] constexpr char gtin_digit(std::string_view body)
{
    if (body.empty()) detail::throw_empty("gtin_digit");
    unsigned sum = 0;
    for (std::size_t k = 0; k < body.size(); ++k)
    {
        const unsigned d = detail::digit_at("gtin_digit", body, body.size() - 1 - k);
        sum += k % 2 == 0 ? 3U * d : d;
    }
    return static_cast<char>('0' + ((10U - (sum % 10U)) % 10U));
}

/// @brief Whether a value ends in its GS1 check digit.
/// @param full Body and check digit.
/// @return True when the check digit is right.
[[nodiscard]] constexpr bool gtin_valid(std::string_view full) noexcept
{
    if (full.size() < 2 || !detail::all_digits(full)) return false;
    return gtin_digit(full.substr(0, full.size() - 1)) == full.back();
}

// --- ICAO Doc 9303 machine-readable zones ------------------------------------------------

/// @brief The check digit of a field of an ICAO 9303 machine-readable zone.
///
/// Weights 7, 3, 1 repeating from the left; `<` counts 0, digits as
/// themselves, A to Z as 10 to 35. The check digit is `sum mod 10`.
///
/// @param field Digits, capital letters and `<`.
/// @return `'0'` to `'9'`.
/// @throws std::invalid_argument If @p field is empty or holds another character.
[[nodiscard]] constexpr char icao9303_digit(std::string_view field)
{
    if (field.empty()) detail::throw_empty("icao9303_digit");
    constexpr std::array<unsigned, 3> weights{7, 3, 1};
    unsigned sum = 0;
    for (std::size_t i = 0; i < field.size(); ++i)
    {
        const char c = field[i];
        unsigned v = 0; // '<'
        if (detail::is_digit(c))
        {
            v = static_cast<unsigned>(c - '0');
        }
        else if (detail::is_capital(c))
        {
            v = static_cast<unsigned>(c - 'A') + 10U;
        }
        else if (c != '<')
        {
            detail::throw_bad_char("icao9303_digit", i, c, "a digit, capital letter or '<'");
        }
        sum += v * weights.at(i % 3);
    }
    return static_cast<char>('0' + (sum % 10U));
}

/// @brief Whether an ICAO 9303 field ends in its check digit.
/// @param field_and_digit The field followed by its check digit.
/// @return True when the check digit is right.
[[nodiscard]] constexpr bool icao9303_valid(std::string_view field_and_digit) noexcept
{
    if (field_and_digit.size() < 2 || !detail::is_digit(field_and_digit.back())) return false;
    const std::string_view field = field_and_digit.substr(0, field_and_digit.size() - 1);
    for (const char c : field)
    {
        if (!detail::is_digit_or_capital(c) && c != '<') return false;
    }
    return icao9303_digit(field) == field_and_digit.back();
}

// --- Verhoeff ---------------------------------------------------------------------------

/// @brief The Verhoeff check digit of a body of digits.
///
/// With the dihedral multiplication table d, the permutation table p and the
/// inverse table inv published by Verhoeff (1969): c = 0; for the digits from
/// the right, with i = 0 for the rightmost, `c = d[c][p[(i + 1) mod 8][digit]]`;
/// the check digit is `inv[c]`.
///
/// @param body Digits, without the check digit.
/// @return `'0'` to `'9'`.
/// @throws std::invalid_argument If @p body is empty or holds a non-digit.
[[nodiscard]] constexpr char verhoeff_digit(std::string_view body)
{
    if (body.empty()) detail::throw_empty("verhoeff_digit");
    std::uint8_t c = 0;
    for (std::size_t k = 0; k < body.size(); ++k)
    {
        const unsigned d = detail::digit_at("verhoeff_digit", body, body.size() - 1 - k);
        c = detail::verhoeff_d.at(c).at(detail::verhoeff_p.at((k + 1) % 8).at(d));
    }
    return static_cast<char>('0' + detail::verhoeff_inv.at(c));
}

/// @brief Whether a value ends in its Verhoeff check digit.
///
/// Runs from the right over the whole value with `p[i mod 8]`; valid when
/// the result is 0.
///
/// @param full Body and check digit.
/// @return True when the check digit is right.
[[nodiscard]] constexpr bool verhoeff_valid(std::string_view full) noexcept
{
    if (full.size() < 2 || !detail::all_digits(full)) return false;
    std::uint8_t c = 0;
    for (std::size_t k = 0; k < full.size(); ++k)
    {
        const auto d = static_cast<unsigned>(full[full.size() - 1 - k] - '0');
        c = detail::verhoeff_d.at(c).at(detail::verhoeff_p.at(k % 8).at(d));
    }
    return c == 0;
}

} // namespace dasmig::core

#endif // DASMIG_CORE_CHECK_DIGITS_HPP
