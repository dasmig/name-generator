// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_TABLE_HPP
#define DASMIG_CORE_TABLE_HPP

#include "rng.hpp"
#include "tsv.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file table.hpp
/// @brief Data tables built from a base layer plus extension layers.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// Every library keeps its data in layered tables, so users can add to it,
/// override it or replace it with files of the same TSV format, without
/// touching the shipped data. See doc/usage.md ("Extending data").

namespace dasmig::core
{

/// @brief How a layer combines with the rows already loaded.
enum class layer_mode : std::uint8_t
{
    add,      ///< New keys only; a key that already exists is an error.
    override, ///< A row replaces the row with the same key; new keys are added.
    replace   ///< The layer replaces every row loaded before it.
};

// Members have {} initializers so designated initializers may leave them
// out without -Wmissing-designated-field-initializers warnings.
// NOLINTBEGIN(readability-redundant-member-init)

/// @brief The columns of a table.
struct table_schema
{
    /// Columns that identify a row, together. Must not be empty.
    std::vector<std::string> key{};
    /// Columns every layer must have (the key columns are always required).
    std::vector<std::string> required{};
    /// Other columns the library reads. Any column not listed here, in
    /// `required` or in `key`, other than `disabled`, is reported as unknown.
    std::vector<std::string> optional{};
};

/// @brief What one loaded layer did.
struct layer_info
{
    std::string source{};              ///< The layer's source name.
    layer_mode mode{layer_mode::override}; ///< How it was applied.
    std::size_t rows{0};               ///< Data rows in the layer.
    std::size_t added{0};              ///< Rows with new keys.
    std::size_t replaced{0};           ///< Rows that replaced an existing key.
    std::size_t removed{0};            ///< Keys removed by `disabled` rows.
    std::uint64_t hash{0};             ///< tsv_document::content_hash().
    std::vector<std::string> warnings{}; ///< Non-fatal problems (unknown columns, ...).
};

// NOLINTEND(readability-redundant-member-init)

/// @brief A table assembled from layers of TSV data.
///
/// - Layers apply in the order loaded; later layers win.
/// - Rows keep the order they first appeared in: an overriding row takes the
///   place of the row it replaces, new rows are appended. Row order is part
///   of the output (weighted choices depend on it).
/// - A row with `disabled` set to `1` removes its key (override and replace
///   modes). A layer needs the key columns; the other required columns are
///   needed only by rows that add or replace data, so a layer that only
///   removes rows can be just `key` and `disabled`.
/// - A layer is validated completely before it is applied; when it fails,
///   the table is unchanged.
///
/// @code
/// dasmig::core::layered_table table{{.key = {"country_code"},
///                                    .required = {"male_height_cm"}}};
/// table.extend(dasmig::core::tsv_document::load("resources/lite/biodata.tsv"),
///              dasmig::core::layer_mode::replace);
/// table.extend(dasmig::core::tsv_document::load("mods/tall_people.tsv"));
/// for (const auto& row : table.rows()) { ... row.get<double>("male_height_cm") ... }
/// @endcode
class layered_table
{
  public:
    /// @brief Name of the column that removes a key when set to 1.
    static constexpr std::string_view disabled_column = "disabled";

    /// @brief An empty table.
    /// @param schema The columns.
    /// @throws std::invalid_argument If the schema has no key column.
    explicit layered_table(table_schema schema) : _schema{std::move(schema)}
    {
        if (_schema.key.empty())
        {
            throw std::invalid_argument("layered_table: the schema needs a key column");
        }
    }

    /// @brief Apply a layer.
    ///
    /// @p validate is called for every row the layer adds or replaces
    /// (not for `disabled` rows) before anything changes; it should parse
    /// the row the way the library will and throw tsv_error on bad data.
    ///
    /// @param doc The layer's data.
    /// @param mode How it combines with the rows loaded before.
    /// @param validate Optional `void(const tsv_row&)` row check.
    /// @return What the layer did.
    /// @throws tsv_error On a missing key column, a missing required column
    ///         (for a row that adds or replaces data), an empty or repeated
    ///         key, an invalid `disabled` value, a clash in add mode, or an
    ///         error from @p validate. The table is then unchanged.
    const layer_info& extend(tsv_document doc, layer_mode mode = layer_mode::override,
                             const std::function<void(const tsv_row&)>& validate = {})
    {
        layer_info info{.source = doc.source(), .mode = mode, .rows = doc.size(),
                        .hash = doc.content_hash()};
        working w;
        w.missing_required = check_columns(doc, info);
        if (mode != layer_mode::replace)
        {
            w.rows = _rows;
            w.index = _index;
        }
        w.removed.assign(w.rows.size(), false);
        w.touched.assign(w.rows.size(), untouched);
        w.rows.reserve(w.rows.size() + doc.size());
        w.index.reserve(w.rows.size() + doc.size());
        const bool has_disabled = doc.has_column(disabled_column);
        bool any_removed = false;
        for (const auto& row : doc.rows())
        {
            any_removed |= apply_row(row, mode, has_disabled, validate, w, info);
        }

        // Compact removed rows and rebuild the index; without removals the
        // working index is already right.
        std::vector<tsv_row> kept;
        key_index kept_index;
        if (any_removed)
        {
            kept.reserve(w.rows.size());
            kept_index.reserve(w.rows.size());
            for (std::size_t i = 0; i < w.rows.size(); ++i)
            {
                if (w.removed[i]) continue;
                kept_index.insert(key_index::hash_of(key_of(w.rows[i])), kept.size());
                kept.push_back(w.rows[i]);
            }
        }
        else
        {
            kept = std::move(w.rows);
            kept_index = std::move(w.index);
        }

        // Commit: nothing below throws except allocation. A replace layer
        // discards everything before it, so the history (and data_hash())
        // starts again: reloading the same base gives the same hash.
        if (mode == layer_mode::replace)
        {
            _documents.clear();
            _layers.clear();
        }
        _documents.push_back(std::move(doc));
        _rows = std::move(kept);
        _index = std::move(kept_index);
        _layers.push_back(std::move(info));
        return _layers.back();
    }

    /// @brief Remove every row and layer.
    void clear() noexcept
    {
        _documents.clear();
        _rows.clear();
        _index.clear();
        _layers.clear();
    }

    /// @brief The rows in effect, in order.
    /// @return The rows.
    [[nodiscard]] const std::vector<tsv_row>& rows() const noexcept { return _rows; }

    /// @brief Number of rows in effect.
    /// @return The count.
    [[nodiscard]] std::size_t size() const noexcept { return _rows.size(); }

    /// @brief Whether no rows are in effect.
    /// @return True when empty.
    [[nodiscard]] bool empty() const noexcept { return _rows.empty(); }

    /// @brief Find a row by key.
    /// @param key The key cells, in schema key order.
    /// @return The row, or empty.
    /// @throws std::invalid_argument If the number of cells is not the number
    ///         of key columns.
    [[nodiscard]] std::optional<tsv_row> find(std::initializer_list<std::string_view> key) const
    {
        if (key.size() != _schema.key.size())
        {
            throw std::invalid_argument("layered_table::find: wrong number of key cells");
        }
        std::string joined;
        std::size_t i = 0;
        for (const auto part : key)
        {
            if (i++ > 0) joined += key_separator;
            joined += part;
        }
        const auto at = locate(_index, _rows, *this, joined);
        if (!at) return std::nullopt;
        return _rows[*at];
    }

    /// @brief The layers in effect, in order: the last replace layer and
    ///        those after it.
    /// @return The layer records.
    [[nodiscard]] const std::vector<layer_info>& layers() const noexcept { return _layers; }

    /// @brief Hash of the loaded layers: their content hashes and modes, in
    ///        order. 0 when nothing is loaded.
    ///
    /// Put it in the seed_signature of results so a replay can tell whether
    /// it runs against the same data.
    ///
    /// @return The hash.
    [[nodiscard]] std::uint64_t data_hash() const noexcept
    {
        std::uint64_t h = 0;
        for (const auto& layer : _layers)
        {
            h = hash_combine(hash_combine(h, layer.hash), static_cast<std::uint64_t>(layer.mode));
        }
        return h;
    }

    /// @brief The schema.
    /// @return The columns.
    [[nodiscard]] const table_schema& schema() const noexcept { return _schema; }

  private:
    static constexpr char key_separator = '\x1f';

    // Keys to row indices, as open addressing over 32-bit slots: 16 bytes
    // per row instead of a node and a copy of the key. A slot keeps the
    // key's hash and its row's index; the key itself is read from the row.
    // Only looked up, never iterated, so the order does not reach the output.
    class key_index
    {
      public:
        [[nodiscard]] static std::uint32_t hash_of(std::string_view key) noexcept
        {
            return static_cast<std::uint32_t>(std::hash<std::string_view>{}(key));
        }

        // The row whose key satisfies @p matches(row_index), among those
        // stored under @p hash.
        template <typename Matches>
        [[nodiscard]] std::optional<std::size_t> find(std::uint32_t hash, const Matches& matches) const
        {
            if (_slots.empty()) return std::nullopt;
            const std::size_t mask = _slots.size() - 1;
            for (std::size_t i = hash & mask;; i = (i + 1) & mask)
            {
                const slot& s = _slots[i];
                if (s.row == vacant) return std::nullopt;
                if (s.hash == hash && matches(static_cast<std::size_t>(s.row))) return s.row;
            }
        }

        // Add a key known to be absent.
        void insert(std::uint32_t hash, std::size_t row)
        {
            if (row >= vacant) throw std::length_error("layered_table: too many rows");
            if ((_count + 1) * 2 > _slots.size()) grow(_slots.empty() ? 16 : _slots.size() * 2);
            place(hash, static_cast<std::uint32_t>(row));
            ++_count;
        }

        void clear() noexcept
        {
            _slots.clear();
            _count = 0;
        }

        void reserve(std::size_t rows)
        {
            std::size_t want = 16;
            while (want < rows * 2) want *= 2;
            if (want > _slots.size()) grow(want);
        }

      private:
        static constexpr std::uint32_t vacant = static_cast<std::uint32_t>(-1);
        struct slot
        {
            std::uint32_t hash{0};
            std::uint32_t row{vacant};
        };

        void place(std::uint32_t hash, std::uint32_t row)
        {
            const std::size_t mask = _slots.size() - 1;
            std::size_t i = hash & mask;
            while (_slots[i].row != vacant) i = (i + 1) & mask;
            _slots[i] = {.hash = hash, .row = row};
        }

        void grow(std::size_t slots)
        {
            const std::vector<slot> old = std::move(_slots);
            _slots.assign(slots, slot{});
            for (const slot& s : old)
            {
                if (s.row != vacant) place(s.hash, s.row);
            }
        }

        std::vector<slot> _slots;
        std::size_t _count{0};
    };

    // Whether @p row has the key @p joined (its key cells joined by the
    // separator), compared without building the row's own key.
    [[nodiscard]] bool key_matches(const tsv_row& row, std::string_view joined) const
    {
        for (std::size_t i = 0; i < _schema.key.size(); ++i)
        {
            if (i > 0)
            {
                if (joined.empty() || joined.front() != key_separator) return false;
                joined.remove_prefix(1);
            }
            const auto cell = row[_schema.key[i]];
            if (!joined.starts_with(cell)) return false;
            joined.remove_prefix(cell.size());
        }
        return joined.empty();
    }

    [[nodiscard]] static std::optional<std::size_t> locate(const key_index& index,
                                                            const std::vector<tsv_row>& rows,
                                                            const layered_table& self,
                                                            std::string_view joined)
    {
        return index.find(key_index::hash_of(joined), [&](std::size_t r) {
            return self.key_matches(rows[r], joined);
        });
    }

    static constexpr std::size_t untouched = static_cast<std::size_t>(-1);

    // The table being built by extend(), applied only when every row passed.
    struct working
    {
        std::vector<tsv_row> rows;
        key_index index;
        std::vector<bool> removed;
        // Per row: the layer line that added, replaced or removed it, or
        // `untouched`. Finds keys repeated within the layer.
        std::vector<std::size_t> touched;
        std::unordered_map<std::string, std::size_t> disabled_unloaded;
        // keys the layer disabled that were not loaded -> line
        std::string missing_required; // a required column the layer lacks, if any
    };

    // Returns true when the row removed a loaded row.
    bool apply_row(const tsv_row& row, layer_mode mode, bool has_disabled,
                   const std::function<void(const tsv_row&)>& validate, working& w,
                   layer_info& info) const
    {
        std::string key = key_of(row);
        const auto existing = locate(w.index, w.rows, *this, key);
        const bool exists = existing.has_value();
        // Keys are unique within a layer, so a key is never removed and added
        // again by the same layer.
        std::size_t first_line = untouched;
        if (exists)
        {
            first_line = w.touched[*existing];
        }
        else if (const auto it = w.disabled_unloaded.find(key); it != w.disabled_unloaded.end())
        {
            first_line = it->second;
        }
        if (first_line != untouched)
        {
            throw row.error("", "key repeated (first on line " + std::to_string(first_line) + ")");
        }

        if (has_disabled && is_disabled(row))
        {
            if (mode == layer_mode::add)
            {
                throw row.error(disabled_column, "a layer in add mode cannot disable rows");
            }
            if (exists)
            {
                w.removed[*existing] = true;
                w.touched[*existing] = row.line();
                ++info.removed;
                return true;
            }
            w.disabled_unloaded.emplace(std::move(key), row.line());
            info.warnings.push_back(row.source() + ":" + std::to_string(row.line()) +
                                    ": disabled key is not loaded");
            return false;
        }
        // Only rows that add or replace data need the required columns; a
        // layer that just disables rows needs the key alone.
        if (!w.missing_required.empty())
        {
            throw row.error(w.missing_required, "required column missing");
        }
        if (validate) validate(row);
        if (exists)
        {
            if (mode == layer_mode::add)
            {
                throw row.error("", "key already loaded (layer is in add mode)");
            }
            w.rows[*existing] = row;
            w.touched[*existing] = row.line();
            ++info.replaced;
            return false;
        }
        w.index.insert(key_index::hash_of(key), w.rows.size());
        w.rows.push_back(row);
        w.removed.push_back(false);
        w.touched.push_back(row.line());
        ++info.added;
        return false;
    }

    // Throws when a key column is missing; returns the first missing required
    // column (empty when none), which only rows with data need.
    std::string check_columns(const tsv_document& doc, layer_info& info) const
    {
        std::set<std::string_view> known{disabled_column};
        std::string missing_required;
        for (const auto& column : _schema.key)
        {
            known.insert(column);
            if (!doc.has_column(column))
            {
                throw tsv_error{doc.source(), 0, column, "key column missing"};
            }
        }
        for (const auto& column : _schema.required)
        {
            known.insert(column);
            if (missing_required.empty() && !doc.has_column(column)) missing_required = column;
        }
        for (const auto& column : _schema.optional) known.insert(column);
        for (const auto& column : doc.columns())
        {
            if (!known.contains(column))
            {
                info.warnings.push_back(doc.source() + ": column '" + column +
                                        "' is not used and was ignored");
            }
        }
        return missing_required;
    }

    [[nodiscard]] std::string key_of(const tsv_row& row) const
    {
        std::string key;
        for (std::size_t i = 0; i < _schema.key.size(); ++i)
        {
            const auto cell = row[_schema.key[i]];
            if (cell.empty())
            {
                throw row.error(_schema.key[i], "key cell is empty");
            }
            if (i > 0) key += key_separator;
            key += cell;
        }
        return key;
    }

    [[nodiscard]] static bool is_disabled(const tsv_row& row)
    {
        const auto value = row[disabled_column];
        if (value.empty() || value == "0") return false;
        if (value == "1") return true;
        throw row.error(disabled_column, "expected 0, 1 or empty, got '" + std::string{value} + "'");
    }

    table_schema _schema;
    std::vector<tsv_document> _documents; // keeps the rows' data alive
    std::vector<tsv_row> _rows;
    key_index _index;
    std::vector<layer_info> _layers;
};

} // namespace dasmig::core

#endif // DASMIG_CORE_TABLE_HPP
