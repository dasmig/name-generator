// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_UTF8_HPP
#define DASMIG_CORE_UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

/// @file utf8.hpp
/// @brief Conversion between UTF-8 and `std::wstring`.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// The core works in UTF-8 (`std::string`). Libraries with a `std::wstring`
/// API convert at the boundary. `wchar_t` is UTF-16 on Windows and UTF-32
/// elsewhere; both are handled, so the UTF-8 is the same on every platform.

namespace dasmig::core
{

/// @brief Whether a byte string is well-formed UTF-8.
///
/// Rejects overlong forms, surrogates (U+D800 to U+DFFF) and code points
/// above U+10FFFF.
///
/// @param text Bytes to check.
/// @return The offset of the first invalid byte, or `std::string_view::npos`
///         when the text is valid.
[[nodiscard]] constexpr std::size_t utf8_invalid_offset(std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto b0 = static_cast<std::uint8_t>(text[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (b0 < 0x80U) { ++i; continue; }
        if ((b0 & 0xE0U) == 0xC0U) { len = 2; cp = b0 & 0x1FU; }
        else if ((b0 & 0xF0U) == 0xE0U) { len = 3; cp = b0 & 0x0FU; }
        else if ((b0 & 0xF8U) == 0xF0U) { len = 4; cp = b0 & 0x07U; }
        else { return i; }
        if (i + len > text.size()) return i;
        for (std::size_t k = 1; k < len; ++k)
        {
            const auto b = static_cast<std::uint8_t>(text[i + k]);
            if ((b & 0xC0U) != 0x80U) return i;
            cp = (cp << 6U) | (b & 0x3FU);
        }
        const bool overlong = (len == 2 && cp < 0x80U) ||
                              (len == 3 && cp < 0x800U) ||
                              (len == 4 && cp < 0x10000U);
        if (overlong || (cp >= 0xD800U && cp <= 0xDFFFU) || cp > 0x10FFFFU)
        {
            return i;
        }
        i += len;
    }
    return std::string_view::npos;
}

/// @brief Whether a byte string is well-formed UTF-8.
/// @param text Bytes to check.
/// @return True when valid.
[[nodiscard]] constexpr bool is_utf8(std::string_view text) noexcept
{
    return utf8_invalid_offset(text) == std::string_view::npos;
}

/// @brief Convert a wide string to UTF-8.
/// @param text UTF-16 (Windows) or UTF-32 text.
/// @return The UTF-8 encoding.
/// @throws std::invalid_argument On an unpaired surrogate or a value above
///         U+10FFFF.
[[nodiscard]] inline std::string to_utf8(std::wstring_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        auto cp = static_cast<std::uint32_t>(text[i]);
        if constexpr (sizeof(wchar_t) == 2)
        {
            cp &= 0xFFFFU;
            if (cp >= 0xD800U && cp <= 0xDBFFU && i + 1 < text.size())
            {
                const auto lo = static_cast<std::uint32_t>(text[i + 1]) & 0xFFFFU;
                if (lo >= 0xDC00U && lo <= 0xDFFFU)
                {
                    cp = 0x10000U + ((cp - 0xD800U) << 10U) + (lo - 0xDC00U);
                    ++i;
                }
            }
        }
        if ((cp >= 0xD800U && cp <= 0xDFFFU) || cp > 0x10FFFFU)
        {
            throw std::invalid_argument("to_utf8: invalid code point");
        }
        if (cp < 0x80U)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800U)
        {
            out += static_cast<char>(0xC0U | (cp >> 6U));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        }
        else if (cp < 0x10000U)
        {
            out += static_cast<char>(0xE0U | (cp >> 12U));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        }
        else
        {
            out += static_cast<char>(0xF0U | (cp >> 18U));
            out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        }
    }
    return out;
}

/// @brief Convert UTF-8 to a wide string.
/// @param text UTF-8 text.
/// @return UTF-16 (Windows) or UTF-32 text.
/// @throws std::invalid_argument If @p text is not well-formed UTF-8.
[[nodiscard]] inline std::wstring from_utf8(std::string_view text)
{
    if (const auto bad = utf8_invalid_offset(text); bad != std::string_view::npos)
    {
        throw std::invalid_argument("from_utf8: invalid UTF-8 at byte " +
                                    std::to_string(bad));
    }
    std::wstring out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto b0 = static_cast<std::uint8_t>(text[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (b0 < 0x80U) { cp = b0; }
        else if ((b0 & 0xE0U) == 0xC0U) { cp = b0 & 0x1FU; len = 2; }
        else if ((b0 & 0xF0U) == 0xE0U) { cp = b0 & 0x0FU; len = 3; }
        else { cp = b0 & 0x07U; len = 4; }
        for (std::size_t k = 1; k < len; ++k)
        {
            cp = (cp << 6U) | (static_cast<std::uint8_t>(text[i + k]) & 0x3FU);
        }
        i += len;
        if constexpr (sizeof(wchar_t) == 2)
        {
            if (cp >= 0x10000U)
            {
                cp -= 0x10000U;
                out += static_cast<wchar_t>(0xD800U + (cp >> 10U));
                out += static_cast<wchar_t>(0xDC00U + (cp & 0x3FFU));
                continue;
            }
        }
        out += static_cast<wchar_t>(cp);
    }
    return out;
}

} // namespace dasmig::core

#endif // DASMIG_CORE_UTF8_HPP
