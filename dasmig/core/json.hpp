// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_JSON_HPP
#define DASMIG_CORE_JSON_HPP

#include "utf8.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// @file json.hpp
/// @brief A minimal, dependency-free JSON writer for `to_json()`.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// Writes compact JSON (RFC 8259). Commas are placed automatically, misuse
/// (a value without a key inside an object, unbalanced ends) throws, and
/// numbers are written with `std::to_chars`, the shortest form that reads
/// back to the same double, so output is identical on every platform.
///
/// JavaScript numbers are doubles: integers above 2^53 lose precision there.
/// Write 64-bit seeds as strings (seed_signature does).

namespace dasmig::core
{

/// @brief Builds one JSON document.
///
/// @code
/// dasmig::core::json_writer w;
/// w.begin_object()
///     .field("name", "Ana")
///     .field("age", 34)
///     .key("tags").begin_array().value("calm").value("curious").end_array()
///  .end_object();
/// std::string text = w.str();   // {"name":"Ana","age":34,"tags":["calm","curious"]}
/// @endcode
class json_writer
{
  public:
    /// @brief An empty writer, with room for a typical document.
    json_writer()
    {
        _out.reserve(initial_capacity_);
        _stack.reserve(initial_depth_);
    }

    /// @brief Open an object.
    /// @return `*this`.
    /// @throws std::logic_error Where a value is not allowed.
    json_writer& begin_object()
    {
        before_value();
        _out += '{';
        _stack.push_back({.type = kind::object});
        return *this;
    }

    /// @brief Close the innermost object.
    /// @return `*this`.
    /// @throws std::logic_error If the innermost container is not an object,
    ///         or a key has no value.
    json_writer& end_object()
    {
        if (_stack.empty() || _stack.back().type != kind::object || _stack.back().key_pending)
        {
            throw std::logic_error("json_writer: end_object without a matching open object");
        }
        _stack.pop_back();
        _out += '}';
        after_value();
        return *this;
    }

    /// @brief Open an array.
    /// @return `*this`.
    /// @throws std::logic_error Where a value is not allowed.
    json_writer& begin_array()
    {
        before_value();
        _out += '[';
        _stack.push_back({.type = kind::array});
        return *this;
    }

    /// @brief Close the innermost array.
    /// @return `*this`.
    /// @throws std::logic_error If the innermost container is not an array.
    json_writer& end_array()
    {
        if (_stack.empty() || _stack.back().type != kind::array)
        {
            throw std::logic_error("json_writer: end_array without a matching open array");
        }
        _stack.pop_back();
        _out += ']';
        after_value();
        return *this;
    }

    /// @brief Write an object key; the next call writes its value.
    /// @param name UTF-8 key.
    /// @return `*this`.
    /// @throws std::logic_error Outside an object or after another key.
    /// @throws std::invalid_argument If @p name is not valid UTF-8.
    json_writer& key(std::string_view name)
    {
        if (_stack.empty() || _stack.back().type != kind::object || _stack.back().key_pending)
        {
            throw std::logic_error("json_writer: key outside an object or after another key");
        }
        if (_stack.back().has_items) _out += ',';
        write_string(name);
        _out += ':';
        _stack.back().key_pending = true;
        return *this;
    }

    /// @brief Write a string value.
    /// @param text UTF-8 text.
    /// @return `*this`.
    /// @throws std::invalid_argument If @p text is not valid UTF-8.
    json_writer& value(std::string_view text)
    {
        before_value();
        write_string(text);
        after_value();
        return *this;
    }

    /// @brief Write a string value (keeps string literals from binding to bool).
    /// @param text Null-terminated UTF-8 text.
    /// @return `*this`.
    json_writer& value(const char* text) { return value(std::string_view{text}); }

    /// @brief Write a boolean.
    /// @param b The value.
    /// @return `*this`.
    json_writer& value(bool b)
    {
        before_value();
        _out += b ? "true" : "false";
        after_value();
        return *this;
    }

    /// @brief Write null.
    /// @return `*this`.
    json_writer& value(std::nullptr_t)
    {
        before_value();
        _out += "null";
        after_value();
        return *this;
    }

    /// @brief Write an integer.
    /// @tparam T Integral type other than bool and the character types.
    /// @param n The value.
    /// @return `*this`.
    template <std::integral T>
        requires(!std::same_as<T, bool> && !std::same_as<T, char> &&
                 !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> &&
                 !std::same_as<T, char16_t> && !std::same_as<T, char32_t>)
    json_writer& value(T n)
    {
        before_value();
        std::array<char, 24> buf{};
        const auto res = std::to_chars(buf.data(), std::to_address(buf.end()), n);
        _out.append(buf.data(), res.ptr);
        after_value();
        return *this;
    }

    /// @brief Write a number, in the shortest form that reads back exactly.
    /// @param d The value.
    /// @return `*this`.
    /// @throws std::invalid_argument If @p d is NaN or infinite (JSON has no
    ///         representation for them).
    json_writer& value(double d)
    {
        if (!std::isfinite(d))
        {
            throw std::invalid_argument("json_writer: NaN and infinity are not JSON");
        }
        before_value();
        std::array<char, 32> buf{};
        const auto res = std::to_chars(buf.data(), std::to_address(buf.end()), d);
        _out.append(buf.data(), res.ptr);
        after_value();
        return *this;
    }

    /// @brief Write a key and its value.
    /// @param name UTF-8 key.
    /// @param v The value (any type value() accepts).
    /// @return `*this`.
    template <typename V>
    json_writer& field(std::string_view name, const V& v)
    {
        key(name);
        return value(v);
    }

    /// @brief Whether exactly one complete value has been written.
    /// @return True when str() can be called.
    [[nodiscard]] bool complete() const noexcept { return _done && _stack.empty(); }

    /// @brief The document.
    /// @return The JSON text.
    /// @throws std::logic_error If the document is incomplete.
    [[nodiscard]] const std::string& str() const
    {
        if (!complete())
        {
            throw std::logic_error("json_writer: document is incomplete");
        }
        return _out;
    }

    /// @brief Append a JSON string literal for @p text to @p out.
    /// @param out Destination.
    /// @param text UTF-8 text.
    /// @throws std::invalid_argument If @p text is not valid UTF-8.
    static void escape(std::string& out, std::string_view text)
    {
        // Most text is printable ASCII without quotes or backslashes: nothing
        // to check or escape. An ASCII prefix is valid UTF-8 on its own, so
        // only the rest needs the check.
        std::size_t plain = 0;
        while (plain < text.size() && needs_no_escape(text[plain]) && static_cast<std::uint8_t>(text[plain]) < 0x80U)
        {
            ++plain;
        }
        if (plain < text.size() && !is_utf8(text.substr(plain)))
        {
            throw std::invalid_argument("json_writer: string is not valid UTF-8");
        }
        constexpr std::string_view hex = "0123456789abcdef";
        out += '"';
        // Bytes that need no escape are appended a run at a time.
        std::size_t run = 0;
        for (std::size_t i = plain; i < text.size(); ++i)
        {
            const char c = text[i];
            if (needs_no_escape(c)) continue;
            const auto b = static_cast<std::uint8_t>(c);
            out.append(text.substr(run, i - run));
            run = i + 1;
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out += "\\u00";
                out += hex[b >> 4U];
                out += hex[b & 0x0FU];
            }
        }
        out.append(text.substr(run));
        out += '"';
    }

  private:
    enum class kind : std::uint8_t
    {
        object,
        array
    };

    // Reserved up front: growing a string from empty reallocates it about ten
    // times on the way to a few kilobytes.
    static constexpr std::size_t initial_capacity_ = 1024;
    static constexpr std::size_t initial_depth_ = 8;

    // A byte written as it is: not a control character, quote or backslash.
    [[nodiscard]] static constexpr bool needs_no_escape(char c) noexcept
    {
        return static_cast<std::uint8_t>(c) >= 0x20U && c != '"' && c != '\\';
    }

    struct frame
    {
        kind type;
        bool has_items{false};
        bool key_pending{false};
    };

    void before_value()
    {
        if (_stack.empty())
        {
            if (_done) throw std::logic_error("json_writer: a document holds one value");
            return;
        }
        auto& top = _stack.back();
        if (top.type == kind::object)
        {
            if (!top.key_pending)
            {
                throw std::logic_error("json_writer: object value without a key");
            }
        }
        else if (top.has_items)
        {
            _out += ',';
        }
    }

    void after_value()
    {
        if (_stack.empty())
        {
            _done = true;
            return;
        }
        auto& top = _stack.back();
        top.has_items = true;
        top.key_pending = false;
    }

    void write_string(std::string_view text) { escape(_out, text); }

    std::string _out;
    std::vector<frame> _stack;
    bool _done{false};
};

} // namespace dasmig::core

#endif // DASMIG_CORE_JSON_HPP
