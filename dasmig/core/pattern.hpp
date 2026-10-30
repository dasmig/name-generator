// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_PATTERN_HPP
#define DASMIG_CORE_PATTERN_HPP

#include "distributions.hpp"
#include "rng.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file pattern.hpp
/// @brief A small pattern language for identifiers: draw a string, test a
///        string, count the strings a pattern can make.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// Letters are literals and the tokens are symbols, so a pattern reads like
/// the value it makes:
///
/// | Syntax | Meaning |
/// | --- | --- |
/// | `#` | a digit, `0`-`9` |
/// | `@` | a capital letter, `A`-`Z` |
/// | `*` | a digit or capital letter, `0`-`9` then `A`-`Z` |
/// | `[...]` | one character of a class, in the order written; `A-Z` ranges; `-` is literal first or last |
/// | `(a\|b)` | one of the alternatives, each a sequence; groups nest |
/// | `{n}`, `{n,m}` | the item before it, exactly n, or n to m times (n may be 0) |
/// | `\x` | the character x as a literal |
/// | anything else | itself |
///
/// pattern::draw()'s use of the engine is part of the core's output contract.

namespace dasmig::core
{

/// @brief A parsed identifier pattern.
///
/// Immutable once parsed, copyable, and safe to share between threads.
/// Parse a library's patterns when its tables load, so a bad pattern is a
/// load error with its row.
///
/// @code
/// static const auto nino = core::pattern::parse("[A-CEGHJ-PR-TW-Z][A-CEGHJ-NPR-TW-Z]######[A-D]");
/// std::string id = nino.draw(r);     // "JG103759A"
/// bool ok = nino.matches(id);        // true
/// @endcode
class pattern
{
  public:
    /// @brief Most characters of pattern text.
    static constexpr std::size_t max_text = 1024;
    /// @brief Deepest nesting of groups.
    static constexpr std::size_t max_depth = 8;
    /// @brief Largest repetition count in a quantifier.
    static constexpr std::size_t max_count = 64;
    /// @brief Most characters in a string the pattern can make.
    static constexpr std::size_t max_length = 256;

    /// @brief The empty pattern, which makes only the empty string.
    pattern() = default;

    /// @brief Parse pattern text.
    /// @param text Printable ASCII pattern (see the file description).
    /// @return The pattern.
    /// @throws std::invalid_argument With the 0-based position, for a byte
    ///         outside printable ASCII, a `|` outside a group, an empty
    ///         alternative, an unclosed `(`, `[` or `{`, a stray `)`, `]` or
    ///         `}`, a dangling `\`, an empty class, a reversed range or a
    ///         repeated character in a class, a quantifier after nothing or
    ///         after another quantifier, a bad count (above 64, m < n or
    ///         m = 0), or text, nesting or strings beyond the limits.
    [[nodiscard]] static pattern parse(std::string_view text)
    {
        if (text.size() > max_text)
        {
            throw std::invalid_argument("pattern::parse: " + std::to_string(text.size()) +
                                        " characters, above " + std::to_string(max_text));
        }
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const auto b = static_cast<std::uint8_t>(text[i]);
            if (b < 0x20U || b > 0x7EU)
            {
                constexpr std::string_view hex = "0123456789ABCDEF";
                parser::fail(i, std::string{"byte 0x"} + hex[b >> 4U] + hex[b & 0x0FU] +
                                    ": patterns are printable ASCII");
            }
        }
        pattern p;
        p._text = std::string{text};
        p._nodes.clear();
        p._sequences.clear();
        p._refs.clear();
        parser ps{.out = &p, .text = text};
        p._root = ps.parse_sequence(0); // at depth 0 it reads to the end or throws
        const stats s = p.sequence_stats(p._root);
        if (s.longest > max_length)
        {
            throw std::invalid_argument(
                "pattern::parse: makes strings of up to " +
                (s.longest == std::numeric_limits<std::uint64_t>::max() ? std::string{"2^64"}
                                                                        : std::to_string(s.longest)) +
                " characters, above " + std::to_string(max_length));
        }
        p._shortest = static_cast<std::size_t>(s.shortest);
        p._longest = static_cast<std::size_t>(s.longest);
        p._count = s.count;
        return p;
    }

    /// @brief Draw a string the pattern can make.
    ///
    /// Left to right: a literal appends itself and draws nothing; a token or
    /// class appends `chars[bounded(r, chars.size())]`, its characters in the
    /// order written (ranges expanded upwards; `#` is `0`-`9`, `@` is `A`-`Z`,
    /// `*` is `0`-`9` then `A`-`Z`); a group draws `k = bounded(r,
    /// alternatives)` and then alternative k; `{n}` draws its item n times,
    /// and `{n,m}` with n < m first draws `c = uniform_int(r, n, m)`, then its
    /// item c times. Alternatives are equally likely whatever their size.
    ///
    /// @param r Engine.
    /// @return The string.
    [[nodiscard]] std::string draw(rng& r) const
    {
        std::string out;
        out.reserve(_longest);
        draw_sequence(_root, r, out);
        return out;
    }

    /// @brief Whether a string is one the pattern can make.
    ///
    /// Computes the set of positions each item can end at, so the time is
    /// polynomial in the length of @p s for any pattern. Check digits are not
    /// checked: call the check-digit function after.
    ///
    /// @param s Candidate string.
    /// @return True when the whole of @p s matches.
    [[nodiscard]] bool matches(std::string_view s) const noexcept
    {
        if (s.size() < _shortest || s.size() > _longest) return false;
        try
        {
            matcher m{*this, s};
            positions start;
            start.set(0);
            return m.sequence_ends(_root, start).test(s.size());
        }
        catch (...)
        {
            return false; // allocation failure
        }
    }

    /// @brief The number of strings the pattern can make.
    ///
    /// Alternatives are counted as disjoint and different repetition counts
    /// as different strings, so it is an upper bound when they overlap.
    /// Saturates at 2^64 - 1. Use it to refuse a unique batch larger than
    /// the space.
    ///
    /// @return The count.
    [[nodiscard]] std::uint64_t count() const noexcept { return _count; }

    /// @brief The shortest and longest strings' lengths.
    /// @return {shortest, longest}.
    [[nodiscard]] std::pair<std::size_t, std::size_t> lengths() const noexcept
    {
        return {_shortest, _longest};
    }

    /// @brief The pattern text, as parsed.
    /// @return The text.
    [[nodiscard]] std::string_view text() const noexcept { return _text; }

    /// @brief Patterns are equal when their text is.
    /// @param a A pattern.
    /// @param b Another.
    /// @return Whether the texts are equal.
    friend bool operator==(const pattern& a, const pattern& b) noexcept
    {
        return a._text == b._text;
    }

  private:
    enum class kind : std::uint8_t
    {
        literal,
        set,
        group
    };

    // One item. A literal or a set holds its characters; a group holds its
    // alternatives (sequence indices) in _refs[first, first + size).
    struct node
    {
        kind type{kind::literal};
        std::uint8_t min{1};
        std::uint8_t max{1};
        std::uint32_t first{0};
        std::uint32_t size{0};
        std::uint32_t group{0}; // ordinal among the groups, for matching
        std::string chars;
        std::array<std::uint64_t, 2> mask{}; // chars as a bitmap of ASCII

        [[nodiscard]] bool has(char c) const noexcept
        {
            const auto b = static_cast<std::uint8_t>(c);
            return b < 128U && ((mask.at(b >> 6U) >> (b & 63U)) & 1U) != 0;
        }
    };

    [[nodiscard]] static node make_node(kind type)
    {
        node n;
        n.type = type;
        return n;
    }

    // Items (node indices) in _refs[first, first + size).
    struct sequence
    {
        std::uint32_t first{0};
        std::uint32_t size{0};
    };

    struct stats
    {
        std::uint64_t shortest;
        std::uint64_t longest;
        std::uint64_t count;
    };

    using positions = std::bitset<max_length + 1>;

    [[nodiscard]] static constexpr std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) noexcept
    {
        return a > std::numeric_limits<std::uint64_t>::max() - b
                   ? std::numeric_limits<std::uint64_t>::max()
                   : a + b;
    }

    [[nodiscard]] static constexpr std::uint64_t sat_mul(std::uint64_t a, std::uint64_t b) noexcept
    {
        if (a == 0 || b == 0) return 0;
        return a > std::numeric_limits<std::uint64_t>::max() / b
                   ? std::numeric_limits<std::uint64_t>::max()
                   : a * b;
    }

    // Recursive-descent parser for the grammar in the file description.
    struct parser
    {
        pattern* out;
        std::string_view text;
        std::size_t pos{0};

        [[noreturn]] static void fail(std::size_t at, const std::string& reason)
        {
            throw std::invalid_argument("pattern::parse: position " + std::to_string(at) + ": " +
                                        reason);
        }

        static std::string quoted(char c) { return std::string{'\''} + c + '\''; }

        [[nodiscard]] std::uint32_t add_node(node n) const
        {
            out->_nodes.push_back(std::move(n));
            return static_cast<std::uint32_t>(out->_nodes.size() - 1);
        }

        [[nodiscard]] std::uint32_t add_set(std::string_view chars) const
        {
            node n = make_node(kind::set);
            for (const char c : chars) add_char(n, c);
            return add_node(std::move(n));
        }

        static void add_char(node& n, char c)
        {
            const auto b = static_cast<std::uint8_t>(c);
            n.chars += c;
            n.mask.at(b >> 6U) |= std::uint64_t{1} << (b & 63U);
        }

        [[nodiscard]] std::uint32_t add_literal(char c) const
        {
            node n = make_node(kind::literal);
            add_char(n, c);
            return add_node(std::move(n));
        }

        // Parse a sequence up to the end, or (inside a group) up to a '|' or
        // ')'; returns its index in _sequences.
        std::uint32_t parse_sequence(std::size_t depth) // NOLINT(misc-no-recursion): groups nest at most max_depth
        {
            std::vector<std::uint32_t> items;
            bool quantified = false;
            while (pos < text.size())
            {
                const char c = text[pos];
                if (c == '|' || c == ')')
                {
                    if (depth == 0) fail(pos, c == '|' ? "'|' outside a group" : "stray ')'");
                    break;
                }
                if (c == '{')
                {
                    if (items.empty()) fail(pos, "quantifier after nothing");
                    if (quantified) fail(pos, "quantifier after another quantifier");
                    quantifier(out->_nodes[items.back()]);
                    quantified = true;
                    continue;
                }
                quantified = false;
                switch (c)
                {
                case '#': items.push_back(add_set("0123456789")); ++pos; break;
                case '@': items.push_back(add_set("ABCDEFGHIJKLMNOPQRSTUVWXYZ")); ++pos; break;
                case '*':
                    items.push_back(add_set("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
                    ++pos;
                    break;
                case '[': items.push_back(char_class()); break;
                case '(': items.push_back(group(depth + 1)); break;
                case ']':
                case '}': fail(pos, "stray " + quoted(c));
                case '\\':
                    if (pos + 1 == text.size()) fail(pos, "dangling '\\'");
                    items.push_back(add_literal(text[pos + 1]));
                    pos += 2;
                    break;
                default: items.push_back(add_literal(c)); ++pos;
                }
            }
            const sequence s{.first = static_cast<std::uint32_t>(out->_refs.size()),
                             .size = static_cast<std::uint32_t>(items.size())};
            out->_refs.insert(out->_refs.end(), items.begin(), items.end());
            out->_sequences.push_back(s);
            return static_cast<std::uint32_t>(out->_sequences.size() - 1);
        }

        std::uint32_t group(std::size_t depth) // NOLINT(misc-no-recursion): as parse_sequence
        {
            const std::size_t start = pos++;
            if (depth > max_depth)
            {
                fail(start, "groups nested deeper than " + std::to_string(max_depth));
            }
            std::vector<std::uint32_t> alternatives;
            for (;;)
            {
                const std::uint32_t s = parse_sequence(depth);
                if (out->_sequences[s].size == 0) fail(pos, "empty alternative");
                alternatives.push_back(s);
                if (pos == text.size()) fail(start, "unclosed '('");
                if (text[pos++] == ')') break; // else '|'
            }
            node n = make_node(kind::group);
            n.first = static_cast<std::uint32_t>(out->_refs.size());
            n.size = static_cast<std::uint32_t>(alternatives.size());
            n.group = static_cast<std::uint32_t>(out->_groups++);
            out->_refs.insert(out->_refs.end(), alternatives.begin(), alternatives.end());
            return add_node(std::move(n));
        }

        std::uint32_t char_class()
        {
            const std::size_t start = pos++;
            node n = make_node(kind::set);
            for (;;)
            {
                if (pos == text.size()) fail(start, "unclosed '['");
                const char c = text[pos];
                if (c == ']') break;
                if (pos + 2 < text.size() && text[pos + 1] == '-' && text[pos + 2] != ']')
                {
                    const char last = text[pos + 2];
                    if (last < c)
                    {
                        fail(pos, "reversed range " + quoted(c) + "-" + quoted(last));
                    }
                    for (char x = c; x <= last; ++x)
                    {
                        if (n.has(x)) fail(pos, "repeated " + quoted(x) + " in class");
                        add_char(n, x);
                    }
                    pos += 3;
                    continue;
                }
                if (n.has(c)) fail(pos, "repeated " + quoted(c) + " in class");
                add_char(n, c);
                ++pos;
            }
            if (n.chars.empty()) fail(start, "empty class");
            ++pos;
            return add_node(std::move(n));
        }

        // A count inside the quantifier opened at @p brace.
        std::size_t number(std::size_t brace)
        {
            const std::size_t start = pos;
            std::size_t value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            {
                value = std::min<std::size_t>((value * 10) + static_cast<std::size_t>(text[pos] - '0'),
                                              max_text);
                ++pos;
            }
            if (pos == text.size()) fail(brace, "unclosed '{'");
            if (pos == start) fail(pos, "expected a count, found " + quoted(text[pos]));
            if (value > max_count)
            {
                fail(start, "count above " + std::to_string(max_count));
            }
            return value;
        }

        void quantifier(node& n)
        {
            const std::size_t start = pos++;
            const std::size_t lo = number(start);
            std::size_t hi = lo;
            if (text[pos] == ',')
            {
                ++pos;
                hi = number(start);
            }
            if (text[pos] != '}') fail(pos, "expected '}', found " + quoted(text[pos]));
            ++pos;
            if (hi < lo) fail(start, "quantifier maximum below its minimum");
            if (hi == 0) fail(start, "quantifier of zero");
            n.min = static_cast<std::uint8_t>(lo);
            n.max = static_cast<std::uint8_t>(hi);
        }
    };

    // The end positions each item can reach in one string, memoised per group
    // and start position.
    struct matcher
    {
        matcher(const pattern& owner, std::string_view text)
            : p{&owner}, s{text}, memo(owner._groups * (text.size() + 1)),
              known(owner._groups * (text.size() + 1))
        {
        }

        const pattern* p;
        std::string_view s;
        std::vector<positions> memo; // group ordinal * (s.size() + 1) + start
        std::vector<char> known;

        // Positions reachable after one occurrence of a node from @p start.
        positions once(const node& n, std::size_t start) // NOLINT(misc-no-recursion): as parse_sequence
        {
            positions ends;
            if (n.type != kind::group)
            {
                if (start < s.size() && n.has(s[start])) ends.set(start + 1);
                return ends;
            }
            const std::size_t slot = (n.group * (s.size() + 1)) + start;
            if (known[slot] != 0) return memo[slot];
            positions from;
            from.set(start);
            for (std::uint32_t a = 0; a < n.size; ++a)
            {
                ends |= sequence_ends(p->_refs[n.first + a], from);
            }
            memo[slot] = ends;
            known[slot] = 1;
            return ends;
        }

        // Positions reachable after an item (with its quantifier) from any
        // position in @p from.
        positions item_ends(const node& n, const positions& from) // NOLINT(misc-no-recursion)
        {
            positions result;
            if (n.min == 0) result = from;
            positions current = from;
            for (std::size_t c = 1; c <= n.max && current.any(); ++c)
            {
                positions next;
                for (std::size_t i = 0; i <= s.size(); ++i)
                {
                    if (current.test(i)) next |= once(n, i);
                }
                current = next;
                if (c >= n.min) result |= current;
            }
            return result;
        }

        positions sequence_ends(std::uint32_t index, positions from) // NOLINT(misc-no-recursion)
        {
            const sequence& seq = p->_sequences[index];
            for (std::uint32_t i = 0; i < seq.size && from.any(); ++i)
            {
                from = item_ends(p->_nodes[p->_refs[seq.first + i]], from);
            }
            return from;
        }
    };

    [[nodiscard]] stats once_stats(const node& n) const // NOLINT(misc-no-recursion): as parse_sequence
    {
        if (n.type == kind::literal) return {.shortest = 1, .longest = 1, .count = 1};
        if (n.type == kind::set) return {.shortest = 1, .longest = 1, .count = n.chars.size()};
        stats g{.shortest = std::numeric_limits<std::uint64_t>::max(), .longest = 0, .count = 0};
        for (std::uint32_t a = 0; a < n.size; ++a)
        {
            const stats s = sequence_stats(_refs[n.first + a]);
            g.shortest = std::min(g.shortest, s.shortest);
            g.longest = std::max(g.longest, s.longest);
            g.count = sat_add(g.count, s.count);
        }
        return g;
    }

    [[nodiscard]] stats sequence_stats(std::uint32_t index) const // NOLINT(misc-no-recursion)
    {
        const sequence& seq = _sequences[index];
        stats total{.shortest = 0, .longest = 0, .count = 1};
        for (std::uint32_t i = 0; i < seq.size; ++i)
        {
            const node& n = _nodes[_refs[seq.first + i]];
            const stats once = once_stats(n);
            // The count of an item is the sum of once.count^c for c in [min, max].
            std::uint64_t item_count = 0;
            std::uint64_t power = 1;
            for (std::size_t c = 0; c <= n.max; ++c)
            {
                if (c >= n.min) item_count = sat_add(item_count, power);
                power = sat_mul(power, once.count);
            }
            total.shortest = sat_add(total.shortest, sat_mul(once.shortest, n.min));
            total.longest = sat_add(total.longest, sat_mul(once.longest, n.max));
            total.count = sat_mul(total.count, item_count);
        }
        return total;
    }

    void draw_sequence(std::uint32_t index, rng& r, std::string& out) const // NOLINT(misc-no-recursion)
    {
        const sequence& seq = _sequences[index];
        for (std::uint32_t i = 0; i < seq.size; ++i)
        {
            const node& n = _nodes[_refs[seq.first + i]];
            const std::uint64_t times =
                n.min == n.max ? n.min : uniform_int<std::uint64_t>(r, n.min, n.max);
            for (std::uint64_t t = 0; t < times; ++t)
            {
                switch (n.type)
                {
                case kind::literal: out += n.chars[0]; break;
                case kind::set:
                    out += n.chars[static_cast<std::size_t>(bounded(r, n.chars.size()))];
                    break;
                case kind::group:
                    draw_sequence(_refs[n.first + static_cast<std::uint32_t>(bounded(r, n.size))], r, out);
                    break;
                }
            }
        }
    }

    std::string _text;
    std::vector<node> _nodes;
    std::vector<sequence> _sequences{sequence{}}; // the empty pattern's root
    std::vector<std::uint32_t> _refs;
    std::uint32_t _root{0};
    std::size_t _groups{0};
    std::size_t _shortest{0};
    std::size_t _longest{0};
    std::uint64_t _count{1};
};

} // namespace dasmig::core

#endif // DASMIG_CORE_PATTERN_HPP
