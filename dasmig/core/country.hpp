// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_COUNTRY_HPP
#define DASMIG_CORE_COUNTRY_HPP

#include <stdexcept>
#include <string>
#include <string_view>

/// @file country.hpp
/// @brief Helpers for ISO 3166-1 alpha-2 country codes.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

namespace dasmig::core
{

/// @brief Whether text has the shape of an ISO 3166-1 alpha-2 code: exactly
///        two capital ASCII letters.
///
/// This checks the shape only. It does not know which codes are assigned or
/// which a library has data for.
///
/// @param code Text to check.
/// @return `true` for `"BR"`, `false` for `"br"`, `"BRA"`, `""` or `"B1"`.
[[nodiscard]] constexpr bool is_alpha2(std::string_view code) noexcept
{
    return code.size() == 2 && code[0] >= 'A' && code[0] <= 'Z' && code[1] >= 'A' && code[1] <= 'Z';
}

/// @brief The exception libraries throw for a country code they have no data
///        for. Its message is `Unknown country code: <code>`.
///
/// @code
/// throw core::unknown_country_error(cca2);
/// @endcode
///
/// @param code The code that was not found.
/// @return A `std::invalid_argument` to throw.
[[nodiscard]] inline std::invalid_argument unknown_country_error(std::string_view code)
{
    return std::invalid_argument{std::string{"Unknown country code: "}.append(code)};
}

} // namespace dasmig::core

#endif // DASMIG_CORE_COUNTRY_HPP
