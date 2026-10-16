// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_SINKS_HPP
#define DASMIG_CORE_SINKS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file sinks.hpp
/// @brief Writers for batches: JSON Lines, and CSV flattened from a result's
///        JSON. Both can be given to for_each_batch() as the sink.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

namespace dasmig::core
{

namespace detail
{

class json_flattener
{
  public:
    explicit json_flattener(std::string_view text) : _text{text} {}

    std::vector<std::pair<std::string, std::string>> run()
    {
        std::vector<std::pair<std::string, std::string>> cells;
        skip_space();
        value("", cells);
        skip_space();
        if (_pos != _text.size()) fail("unexpected text after the value");
        return cells;
    }

  private:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static std::string child_path(const std::string& path, const std::string& key)
    {
        if (path.empty()) return key;
        std::string out = path;
        out += '.';
        out += key;
        return out;
    }

    [[noreturn]] void fail(const char* what) const
    {
        throw std::invalid_argument("flatten_json: " + std::string{what} + " at offset " +
                                    std::to_string(_pos));
    }

    void skip_space()
    {
        while (_pos < _text.size() &&
               (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' || _text[_pos] == '\r'))
        {
            ++_pos;
        }
    }

    [[nodiscard]] char peek() const
    {
        if (_pos >= _text.size()) fail("unexpected end");
        return _text[_pos];
    }

    static void append_utf8(std::string& out, std::uint32_t cp)
    {
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

    std::uint32_t hex4()
    {
        if (_pos + 4 > _text.size()) fail("short \\u escape");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = _text[_pos++];
            v <<= 4U;
            if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string string_value()
    {
        if (peek() != '"') fail("expected a string");
        ++_pos;
        std::string out;
        for (;;)
        {
            const char c = peek();
            ++_pos;
            if (c == '"') return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            const char e = peek();
            ++_pos;
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                std::uint32_t cp = hex4();
                if (cp >= 0xD800U && cp <= 0xDBFFU && _pos + 1 < _text.size() &&
                    _text[_pos] == '\\' && _text[_pos + 1] == 'u')
                {
                    _pos += 2;
                    const std::uint32_t lo = hex4();
                    if (lo < 0xDC00U || lo > 0xDFFFU) fail("bad surrogate pair");
                    cp = 0x10000U + ((cp - 0xD800U) << 10U) + (lo - 0xDC00U);
                }
                append_utf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
    }

    // Moves past one value of any kind without keeping it.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity, misc-no-recursion)
    void skip_value()
    {
        skip_space();
        const char c = peek();
        if (c == '"')
        {
            (void)string_value();
        }
        else if (c == '{' || c == '[')
        {
            const char close = c == '{' ? '}' : ']';
            ++_pos;
            skip_space();
            if (peek() == close)
            {
                ++_pos;
                return;
            }
            for (;;)
            {
                if (c == '{')
                {
                    skip_space();
                    (void)string_value();
                    skip_space();
                    if (peek() != ':') fail("expected ':'");
                    ++_pos;
                }
                skip_value();
                skip_space();
                const char sep = peek();
                ++_pos;
                if (sep == close) return;
                if (sep != ',') fail("expected ',' or the end of the container");
            }
        }
        else
        {
            const std::size_t start = _pos;
            while (_pos < _text.size() && _text[_pos] != ',' && _text[_pos] != '}' &&
                   _text[_pos] != ']' && _text[_pos] != ' ' && _text[_pos] != '\n' &&
                   _text[_pos] != '\r' && _text[_pos] != '\t')
            {
                ++_pos;
            }
            if (_pos == start) fail("expected a value");
        }
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity, misc-no-recursion)
    void value(const std::string& path, std::vector<std::pair<std::string, std::string>>& cells)
    {
        const char c = peek();
        if (c == '{')
        {
            const std::size_t start = _pos;
            ++_pos;
            skip_space();
            if (peek() == '}')
            {
                ++_pos;
                if (!path.empty()) cells.emplace_back(path, std::string{_text.substr(start, 2)});
                return;
            }
            for (;;)
            {
                skip_space();
                const std::string key = string_value();
                skip_space();
                if (peek() != ':') fail("expected ':'");
                ++_pos;
                skip_space();
                value(child_path(path, key), cells);
                skip_space();
                const char sep = peek();
                ++_pos;
                if (sep == '}') return;
                if (sep != ',') fail("expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            const std::size_t start = _pos;
            skip_value();
            cells.emplace_back(path, std::string{_text.substr(start, _pos - start)});
            return;
        }
        if (c == '"')
        {
            cells.emplace_back(path, string_value());
            return;
        }
        const std::size_t start = _pos;
        skip_value();
        const std::string_view token = _text.substr(start, _pos - start);
        cells.emplace_back(path, token == "null" ? std::string{} : std::string{token});
    }

    std::string_view _text;
    std::size_t _pos{0};
};

} // namespace detail

/// @brief Flatten a JSON document into dotted columns.
///
/// An object's members become `parent.child` cells, strings are unescaped,
/// numbers and booleans keep their text, `null` is an empty cell, and an array
/// (or an empty nested object) is one cell holding its JSON text. A document
/// that is not an object gives one cell with an empty name.
///
/// @param json A complete JSON document.
/// @return The `(column, cell)` pairs in document order.
/// @throws std::invalid_argument If @p json is not valid JSON.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> flatten_json(
    std::string_view json)
{
    return detail::json_flattener{json}.run();
}

/// @brief Whether a result has `to_json()` returning text.
template <typename T>
concept json_result = requires(const T& item) {
    { item.to_json() } -> std::convertible_to<std::string_view>;
};

/// @brief Writes one JSON document per line (JSON Lines / NDJSON).
///
/// Use it as the sink of for_each_batch(); results need `to_json()`.
///
/// @code
/// dasmig::core::jsonl_writer out{std::cout};
/// gen.for_each_name(request, {.count = 1000}, 7, out);
/// @endcode
class jsonl_writer
{
  public:
    /// @brief Write to @p out, which must outlive the writer.
    /// @param out Destination stream.
    explicit jsonl_writer(std::ostream& out) : _out{&out} {}

    /// @brief Write one document as a line.
    /// @param json Compact JSON; it must not contain a line break.
    /// @throws std::invalid_argument If @p json contains a line break.
    void write_line(std::string_view json)
    {
        if (json.find_first_of("\r\n") != std::string_view::npos)
        {
            throw std::invalid_argument("jsonl_writer: a JSON Lines record cannot contain a line break");
        }
        _out->write(json.data(), static_cast<std::streamsize>(json.size()));
        _out->put('\n');
        ++_lines;
    }

    /// @brief Sink call: write the item's `to_json()`.
    /// @param item The result.
    template <json_result T>
    void operator()(std::uint64_t /*index*/, const T& item)
    {
        write_line(item.to_json());
    }

    /// @brief Lines written so far.
    /// @return The count.
    [[nodiscard]] std::size_t lines() const noexcept { return _lines; }

  private:
    std::ostream* _out;
    std::size_t _lines{0};
};

/// @brief Writes results as CSV, one row each, with columns flattened from
///        their JSON (see flatten_json()).
///
/// The header comes from the first record. Later records are matched to it by
/// column name: a column the record lacks is empty, and a column the header
/// lacks is dropped. Fields are quoted when they hold a comma, a quote or a
/// line break; rows end with `\n`.
class csv_writer
{
  public:
    /// @brief Write to @p out, which must outlive the writer.
    /// @param out Destination stream.
    explicit csv_writer(std::ostream& out) : _out{&out} {}

    /// @brief Write one record given as JSON.
    /// @param json A JSON document.
    /// @throws std::invalid_argument If @p json is not valid JSON.
    void write_json(std::string_view json)
    {
        const auto cells = flatten_json(json);
        if (!_header_written)
        {
            _columns.reserve(cells.size());
            for (const auto& cell : cells) _columns.push_back(cell.first);
            write_row(_columns);
            _header_written = true;
        }
        std::vector<std::string> row(_columns.size());
        bool in_order = cells.size() == _columns.size();
        for (std::size_t i = 0; in_order && i < cells.size(); ++i)
        {
            in_order = cells[i].first == _columns[i];
        }
        if (in_order)
        {
            for (std::size_t i = 0; i < cells.size(); ++i) row[i] = cells[i].second;
        }
        else
        {
            std::unordered_map<std::string_view, std::size_t> position;
            position.reserve(_columns.size());
            for (std::size_t i = 0; i < _columns.size(); ++i) position.emplace(_columns[i], i);
            for (const auto& cell : cells)
            {
                if (const auto it = position.find(cell.first); it != position.end())
                {
                    row[it->second] = cell.second;
                }
            }
        }
        write_row(row);
        ++_records;
    }

    /// @brief Sink call: write the item's `to_json()`.
    /// @param item The result.
    template <json_result T>
    void operator()(std::uint64_t /*index*/, const T& item)
    {
        write_json(item.to_json());
    }

    /// @brief The columns, known after the first record.
    /// @return The column names.
    [[nodiscard]] const std::vector<std::string>& columns() const noexcept { return _columns; }

    /// @brief Records written so far (not counting the header).
    /// @return The count.
    [[nodiscard]] std::size_t records() const noexcept { return _records; }

  private:
    void write_row(const std::vector<std::string>& fields)
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (i > 0) _out->put(',');
            write_field(fields[i]);
        }
        _out->put('\n');
    }

    void write_field(const std::string& field)
    {
        if (field.find_first_of(",\"\r\n") == std::string::npos)
        {
            _out->write(field.data(), static_cast<std::streamsize>(field.size()));
            return;
        }
        _out->put('"');
        for (const char c : field)
        {
            if (c == '"') _out->put('"');
            _out->put(c);
        }
        _out->put('"');
    }

    std::ostream* _out;
    std::vector<std::string> _columns;
    bool _header_written{false};
    std::size_t _records{0};
};

} // namespace dasmig::core

#endif // DASMIG_CORE_SINKS_HPP
