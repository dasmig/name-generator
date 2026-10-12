#ifndef DASMIG_CORE_TSV_HPP
#define DASMIG_CORE_TSV_HPP

#include "rng.hpp"
#include "utf8.hpp"

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

/// @file tsv.hpp
/// @brief Tab-separated data files with typed access and located errors.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// The format every library's `resources/` and every extension layer uses:
///
/// - UTF-8, with or without a byte order mark; LF or CRLF line ends.
/// - The first line names the columns, unless the columns are given to
///   parse() (for headerless word lists).
/// - Cells are separated by one tab and are not quoted. A row may have fewer
///   cells than columns (the missing ones are empty), not more.
/// - Empty lines and lines starting with `#` are skipped.
///
/// Numbers are parsed exactly (correctly rounded), so the same file gives the
/// same doubles on every platform.

namespace dasmig::core
{

/// @brief A data error with its location.
///
/// `what()` reads `source:line: column 'name': message`, leaving out the
/// parts that do not apply.
class tsv_error : public std::runtime_error
{
  public:
    /// @brief Construct.
    /// @param source File name or other description of the data.
    /// @param line 1-based line number, or 0 for the whole source.
    /// @param column Column name, or empty for the whole line.
    /// @param message What is wrong.
    tsv_error(std::string source, std::size_t line, std::string column,
              const std::string& message)
        : std::runtime_error{format(source, line, column, message)},
          _source{std::move(source)}, _line{line}, _column{std::move(column)}
    {
    }

    /// @brief The data source.
    /// @return File name or description.
    [[nodiscard]] const std::string& source() const noexcept { return _source; }

    /// @brief The line.
    /// @return 1-based line, or 0 for the whole source.
    [[nodiscard]] std::size_t line() const noexcept { return _line; }

    /// @brief The column.
    /// @return Column name, or empty for the whole line.
    [[nodiscard]] const std::string& column() const noexcept { return _column; }

  private:
    static std::string format(const std::string& source, std::size_t line,
                              const std::string& column, const std::string& message) // NOLINT(bugprone-easily-swappable-parameters)
    {
        std::string out = source;
        if (line > 0) out += ":" + std::to_string(line);
        out += ": ";
        if (!column.empty()) out += "column '" + column + "': ";
        return out + message;
    }

    std::string _source;
    std::size_t _line;
    std::string _column;
};

/// @cond INTERNAL
namespace detail
{

// Whole-cell number parsing; an optional leading '+' is accepted.
template <typename T>
[[nodiscard]] std::optional<T> parse_number(std::string_view text)
{
    if (text.size() > 1 && text.front() == '+' && text[1] != '-') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    T value{};
    if constexpr (std::floating_point<T>)
    {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        const auto res = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value);
        if (res.ec != std::errc{} || res.ptr != std::to_address(text.end())) return std::nullopt;
#else
        // Standard libraries without floating-point from_chars (older libc++):
        // the classic locale's strtod is correctly rounded on those platforms.
        std::istringstream in{std::string{text}};
        in.imbue(std::locale::classic());
        in >> value;
        if (in.fail() || in.peek() != std::char_traits<char>::eof()) return std::nullopt;
#endif
        if (!std::isfinite(value)) return std::nullopt;
    }
    else
    {
        const auto res = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value);
        if (res.ec != std::errc{} || res.ptr != std::to_address(text.end())) return std::nullopt;
    }
    return value;
}

// The parsed content of a document, shared by its copies and its rows.
struct tsv_data
{
    std::string text;
    std::string source;
    std::vector<std::string> columns;
    std::map<std::string, std::size_t, std::less<>> column_index;
    std::vector<std::string_view> cells;  // rows * columns, into text
    std::vector<std::size_t> row_lines;   // 1-based
    std::uint64_t hash{0};

    [[nodiscard]] std::optional<std::size_t> find(std::string_view name) const
    {
        const auto it = column_index.find(name);
        if (it == column_index.end()) return std::nullopt;
        return it->second;
    }
};

} // namespace detail
/// @endcond

class tsv_document;

/// @brief One row of a tsv_document. A light view: copy it freely while the
///        document, or any copy of it, is alive.
class tsv_row
{
  public:
    /// @brief The cell in a named column.
    /// @param column Column name.
    /// @return The cell text (empty for a missing trailing cell).
    /// @throws tsv_error If the document has no such column.
    [[nodiscard]] std::string_view operator[](std::string_view column) const;

    /// @brief The cell at a column index.
    /// @param index 0-based column index.
    /// @return The cell text.
    /// @throws std::out_of_range If @p index is not a column.
    [[nodiscard]] std::string_view cell(std::size_t index) const;

    /// @brief Whether the document has a column and this row's cell in it is
    ///        not empty.
    /// @param column Column name.
    /// @return True when a value is present.
    [[nodiscard]] bool has(std::string_view column) const;

    /// @brief A typed value.
    ///
    /// Integers are base 10; doubles are decimal or scientific and must be
    /// finite; bool accepts `1`, `0`, `true` and `false`; strings are
    /// returned as they are.
    ///
    /// @tparam T Arithmetic type, bool, `std::string` or `std::string_view`.
    /// @param column Column name.
    /// @return The value.
    /// @throws tsv_error If the column is missing, the cell is empty, or the
    ///         text is not a valid T.
    template <typename T>
    [[nodiscard]] T get(std::string_view column) const
    {
        const std::string_view text = (*this)[column];
        if (text.empty())
        {
            throw error(column, "missing value");
        }
        return convert<T>(column, text);
    }

    /// @brief A typed value, or a fallback when the column is missing or the
    ///        cell is empty.
    /// @tparam T As get().
    /// @param column Column name.
    /// @param fallback Value for a missing or empty cell.
    /// @return The value.
    /// @throws tsv_error If the text is not a valid T.
    template <typename T>
    [[nodiscard]] T get_or(std::string_view column, T fallback) const
    {
        if (!has(column)) return fallback;
        return convert<T>(column, (*this)[column]);
    }

    /// @brief Split a list cell.
    /// @param column Column name.
    /// @param separator List separator (`;` in the shipped data).
    /// @return The items; empty for an empty cell. Items are not trimmed.
    /// @throws tsv_error If the column is missing.
    [[nodiscard]] std::vector<std::string_view> list(std::string_view column,
                                                     char separator = ';') const
    {
        std::vector<std::string_view> items;
        std::string_view text = (*this)[column];
        if (text.empty()) return items;
        for (;;)
        {
            const auto pos = text.find(separator);
            items.push_back(text.substr(0, pos));
            if (pos == std::string_view::npos) break;
            text.remove_prefix(pos + 1);
        }
        return items;
    }

    /// @brief The 1-based line of this row in its source.
    /// @return The line number.
    [[nodiscard]] std::size_t line() const noexcept;

    /// @brief The source of this row's document.
    /// @return File name or description.
    [[nodiscard]] const std::string& source() const noexcept;

    /// @brief An error located at this row.
    /// @param column Column name, or empty for the whole row.
    /// @param message What is wrong.
    /// @return The error, for throwing.
    [[nodiscard]] tsv_error error(std::string_view column, const std::string& message) const
    {
        return tsv_error{source(), line(), std::string{column}, message};
    }

  private:
    friend class tsv_document;

    tsv_row(const detail::tsv_data* data, std::size_t index) noexcept
        : _data{data}, _index{index} {}

    template <typename T>
    [[nodiscard]] T convert(std::string_view column, std::string_view text) const
    {
        if constexpr (std::same_as<T, std::string_view>)
        {
            return text;
        }
        else if constexpr (std::same_as<T, std::string>)
        {
            return std::string{text};
        }
        else if constexpr (std::same_as<T, bool>)
        {
            if (text == "1" || text == "true") return true;
            if (text == "0" || text == "false") return false;
            throw error(column, "expected 0, 1, true or false, got '" + std::string{text} + "'");
        }
        else
        {
            static_assert(std::is_arithmetic_v<T>, "tsv_row::get: unsupported type");
            if (const auto v = detail::parse_number<T>(text)) return *v;
            throw error(column, "expected a number, got '" + std::string{text} + "'");
        }
    }

    const detail::tsv_data* _data;
    std::size_t _index;
};

/// @brief A parsed tab-separated file.
///
/// Copies share the text, so rows taken from one copy stay valid while any
/// copy is alive.
class tsv_document
{
  public:
    /// @brief Parse text whose first line names the columns.
    /// @param text UTF-8 text.
    /// @param source Name used in errors (a file name, or a description such
    ///        as "layer from host").
    /// @return The document.
    /// @throws tsv_error On invalid UTF-8, no header, an empty or repeated
    ///         column name, or a row with more cells than columns.
    [[nodiscard]] static tsv_document parse(std::string_view text, std::string source)
    {
        return tsv_document{std::string{text}, std::move(source), {}};
    }

    /// @brief Parse headerless text with the given column names.
    /// @param text UTF-8 text.
    /// @param source Name used in errors.
    /// @param columns Column names.
    /// @return The document.
    /// @throws tsv_error As the other overload.
    [[nodiscard]] static tsv_document parse(std::string_view text, std::string source,
                                            std::vector<std::string> columns)
    {
        if (columns.empty())
        {
            throw tsv_error{std::move(source), 0, "", "no columns given"};
        }
        return tsv_document{std::string{text}, std::move(source), std::move(columns)};
    }

    /// @brief Read and parse a file.
    /// @param path File to read; it is also the source name in errors.
    /// @param columns Column names for a headerless file; empty when the
    ///        first line names them.
    /// @return The document.
    /// @throws tsv_error If the file cannot be read, or as parse().
    [[nodiscard]] static tsv_document load(const std::filesystem::path& path,
                                           std::vector<std::string> columns = {})
    {
        std::ifstream in{path, std::ios::binary};
        if (!in)
        {
            throw tsv_error{path.generic_string(), 0, "", "cannot open file"};
        }
        std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        if (columns.empty())
        {
            return tsv_document{std::move(text), path.generic_string(), {}};
        }
        return tsv_document{std::move(text), path.generic_string(), std::move(columns)};
    }

    /// @brief The column names, in file order.
    /// @return The columns.
    [[nodiscard]] const std::vector<std::string>& columns() const noexcept
    {
        return _data->columns;
    }

    /// @brief The index of a column.
    /// @param name Column name.
    /// @return The 0-based index, or empty when there is no such column.
    [[nodiscard]] std::optional<std::size_t> column_index(std::string_view name) const
    {
        return _data->find(name);
    }

    /// @brief Whether a column exists.
    /// @param name Column name.
    /// @return True when present.
    [[nodiscard]] bool has_column(std::string_view name) const
    {
        return column_index(name).has_value();
    }

    /// @brief Number of data rows.
    /// @return The row count.
    [[nodiscard]] std::size_t size() const noexcept { return _data->row_lines.size(); }

    /// @brief Whether there are no data rows.
    /// @return True when empty.
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    /// @brief A row.
    /// @param i 0-based row index.
    /// @return The row.
    /// @throws std::out_of_range If @p i >= size().
    [[nodiscard]] tsv_row row(std::size_t i) const
    {
        if (i >= size()) throw std::out_of_range("tsv_document::row: index out of range");
        return tsv_row{_data.get(), i};
    }

    /// @brief All rows.
    /// @return The rows, in file order.
    [[nodiscard]] std::vector<tsv_row> rows() const
    {
        std::vector<tsv_row> out;
        out.reserve(size());
        for (std::size_t i = 0; i < size(); ++i) out.push_back(tsv_row{_data.get(), i});
        return out;
    }

    /// @brief The source name used in errors.
    /// @return File name or description.
    [[nodiscard]] const std::string& source() const noexcept { return _data->source; }

    /// @brief Hash of the parsed content: columns and cells, without
    ///        comments, blank lines, line-end style or byte order mark.
    ///
    /// The same data checked out with LF or CRLF line ends hashes the same.
    ///
    /// @return 64-bit FNV-1a of the canonical content.
    [[nodiscard]] std::uint64_t content_hash() const noexcept { return _data->hash; }

  private:
    tsv_document(std::string text, std::string source, std::vector<std::string> columns)
        : _data{std::make_shared<detail::tsv_data>()}
    {
        auto& d = *_data;
        d.text = std::move(text);
        d.source = std::move(source);
        if (const auto bad = utf8_invalid_offset(d.text); bad != std::string_view::npos)
        {
            throw tsv_error{d.source, line_of(d.text, bad), "", "invalid UTF-8"};
        }
        std::string_view rest = d.text;
        if (rest.starts_with("\xEF\xBB\xBF")) rest.remove_prefix(3);

        const bool header = columns.empty();
        d.columns = std::move(columns);
        std::size_t line = 0;
        bool have_header = !header;
        if (!header) index_columns(0);

        while (!rest.empty())
        {
            ++line;
            const auto end = rest.find('\n');
            std::string_view text_line = rest.substr(0, end);
            rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
            if (text_line.ends_with('\r')) text_line.remove_suffix(1);
            if (text_line.empty() || text_line.front() == '#') continue;

            if (!have_header)
            {
                for (const auto cell : split(text_line)) d.columns.emplace_back(cell);
                index_columns(line);
                have_header = true;
                continue;
            }
            const auto cells = split(text_line);
            if (cells.size() > d.columns.size())
            {
                throw tsv_error{d.source, line, "",
                                "row has " + std::to_string(cells.size()) + " cells but there are " +
                                    std::to_string(d.columns.size()) + " columns"};
            }
            d.cells.insert(d.cells.end(), cells.begin(), cells.end());
            d.cells.insert(d.cells.end(), d.columns.size() - cells.size(), std::string_view{});
            d.row_lines.push_back(line);
        }
        if (!have_header)
        {
            throw tsv_error{d.source, 0, "", "no header line"};
        }
        d.hash = canonical_hash();
    }

    static std::vector<std::string_view> split(std::string_view line)
    {
        std::vector<std::string_view> cells;
        for (;;)
        {
            const auto pos = line.find('\t');
            cells.push_back(line.substr(0, pos));
            if (pos == std::string_view::npos) break;
            line.remove_prefix(pos + 1);
        }
        return cells;
    }

    static std::size_t line_of(std::string_view text, std::size_t offset)
    {
        std::size_t line = 1;
        for (std::size_t i = 0; i < offset && i < text.size(); ++i)
        {
            if (text[i] == '\n') ++line;
        }
        return line;
    }

    void index_columns(std::size_t line)
    {
        auto& d = *_data;
        for (std::size_t i = 0; i < d.columns.size(); ++i)
        {
            if (d.columns[i].empty())
            {
                throw tsv_error{d.source, line, "", "column " + std::to_string(i + 1) + " has no name"};
            }
            if (!d.column_index.emplace(d.columns[i], i).second)
            {
                throw tsv_error{d.source, line, d.columns[i], "column name repeated"};
            }
        }
    }

    [[nodiscard]] std::uint64_t canonical_hash() const
    {
        const auto& d = *_data;
        std::uint64_t h = 0xcbf29ce484222325ULL;
        auto feed = [&h](std::string_view s) {
            for (const char c : s)
            {
                h ^= static_cast<std::uint8_t>(c);
                h *= 0x100000001b3ULL;
            }
        };
        auto feed_row = [&](auto first, auto last) {
            for (auto it = first; it != last; ++it)
            {
                if (it != first) feed("\t");
                feed(*it);
            }
            feed("\n");
        };
        feed_row(d.columns.begin(), d.columns.end());
        const auto width = static_cast<std::ptrdiff_t>(d.columns.size());
        for (std::size_t r = 0; r < d.row_lines.size(); ++r)
        {
            const auto first = d.cells.begin() + (static_cast<std::ptrdiff_t>(r) * width);
            feed_row(first, first + width);
        }
        return h;
    }

    std::shared_ptr<detail::tsv_data> _data;
};

inline std::string_view tsv_row::operator[](std::string_view column) const
{
    const auto index = _data->find(column);
    if (!index)
    {
        throw error(column, "no such column");
    }
    return cell(*index);
}

inline std::string_view tsv_row::cell(std::size_t index) const
{
    const auto width = _data->columns.size();
    if (index >= width) throw std::out_of_range("tsv_row::cell: index out of range");
    return _data->cells[(_index * width) + index];
}

inline bool tsv_row::has(std::string_view column) const
{
    const auto index = _data->find(column);
    return index && !cell(*index).empty();
}

inline std::size_t tsv_row::line() const noexcept { return _data->row_lines[_index]; }

inline const std::string& tsv_row::source() const noexcept { return _data->source; }

} // namespace dasmig::core

#endif // DASMIG_CORE_TSV_HPP
