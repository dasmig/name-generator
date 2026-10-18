// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_SEX_HPP
#define DASMIG_CORE_SEX_HPP

#include <cstdint>
#include <optional>
#include <string_view>

/// @file sex.hpp
/// @brief The sex enum shared by every generator library, and its text form.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

// Libraries released before the core define their own copy of the enum behind
// this guard; defining it here makes them skip it when included together.
#ifndef DASMIG_SEX_DEFINED
#define DASMIG_SEX_DEFINED

namespace dasmig
{

/// @brief Biological sex.
enum class sex : std::uint8_t
{
    male,  ///< Male.
    female ///< Female.
};

} // namespace dasmig

#endif // DASMIG_SEX_DEFINED

namespace dasmig::core
{

/// @brief The lower-case name of a sex, as written in data files and JSON.
/// @param s Sex.
/// @return `"male"` or `"female"`; `"unknown"` for a value outside the enum.
[[nodiscard]] constexpr std::string_view sex_name(sex s) noexcept
{
    switch (s)
    {
    case sex::male: return "male";
    case sex::female: return "female";
    }
    return "unknown";
}

/// @brief Reads the name written by sex_name().
///
/// Exact and case-sensitive: only `"male"` and `"female"` are accepted.
///
/// @param text Text to read.
/// @return The sex, or an empty optional for any other text.
[[nodiscard]] constexpr std::optional<sex> parse_sex(std::string_view text) noexcept
{
    if (text == "male") return sex::male;
    if (text == "female") return sex::female;
    return std::nullopt;
}

} // namespace dasmig::core

#endif // DASMIG_CORE_SEX_HPP
