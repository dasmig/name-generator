// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_TZ_HPP
#define DASMIG_CORE_TZ_HPP

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

/// @file tz.hpp
/// @brief Time-zone rules: a list of periods plus a POSIX TZ rule, the offset
///        at an instant, and local time to UTC and back.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
///
/// `std::chrono::tzdb` reads the operating system's time-zone database, which
/// differs by platform and update level and is missing from Emscripten and
/// older libc++, so the same seed would give different local times on
/// different machines. A tz_rules holds a zone's rules as plain text the
/// caller gives it (country-generator ships them), and uses only
/// `std::chrono`'s durations and calendar arithmetic.
///
/// Text form: periods are `utc-seconds/offset-seconds/abbreviation/s|d`
/// separated by single spaces, with strictly increasing starts; the offset is
/// east of UTC, and `s` or `d` says standard or daylight time. The POSIX rule
/// (the footer of a TZif file, RFC 8536) applies after the last period. For
/// example, Berlin from 2000:
///
/// @code
/// auto berlin = core::tz_rules::parse(
///     "946684800/3600/CET/s 954032400/7200/CEST/d 972781200/3600/CET/s",
///     "CET-1CEST,M3.5.0,M10.5.0/3");
/// auto info = berlin.at(std::chrono::sys_days{2024y / 7 / 1});   // +02:00, CEST
/// @endcode
///
/// Instants from the first period's start (valid_from()) to the end of the
/// year 9999 are supported.

namespace dasmig::core
{

/// @brief Which instant to_utc() returns for a local time that occurs twice.
enum class tz_choose : std::uint8_t
{
    earliest, ///< The first occurrence (still on daylight time, when clocks go back).
    latest    ///< The second occurrence.
};

/// @brief The rules in force at an instant.
struct tz_info
{
    std::chrono::seconds offset;   ///< East of UTC.
    bool dst;                      ///< Daylight time.
    std::string_view abbreviation; ///< Valid while the tz_rules it came from lives.

    /// @brief Equal when the offset, the flag and the abbreviation text are.
    friend bool operator==(const tz_info&, const tz_info&) = default;
};

/// @cond INTERNAL
namespace detail
{

inline constexpr std::chrono::sys_seconds tz_first{
    std::chrono::sys_days{std::chrono::year{-9999} / 1 / 1}};
inline constexpr std::chrono::sys_seconds tz_last{
    std::chrono::sys_days{std::chrono::year{10000} / 1 / 1} - std::chrono::seconds{1}};

// "2024-03-31T01:00:00Z"; years below 0 with a minus, padded to 4 digits.
// Instants far outside the supported years (whose calendar date would
// overflow chrono's day count) are written as seconds since the epoch.
[[nodiscard]] inline std::string iso_utc(std::chrono::sys_seconds t)
{
    constexpr std::chrono::days margin{366};
    if (t < tz_first - margin || t > tz_last + margin)
    {
        return std::to_string(t.time_since_epoch().count()) + " s after 1970-01-01T00:00:00Z";
    }
    const auto day = std::chrono::floor<std::chrono::days>(t);
    const std::chrono::year_month_day ymd{day};
    const auto secs = (t - day).count();
    const auto two = [](long long v) {
        return std::string{static_cast<char>('0' + (v / 10)), static_cast<char>('0' + (v % 10))};
    };
    int y = static_cast<int>(ymd.year());
    std::string out = y < 0 ? "-" : "";
    y = y < 0 ? -y : y;
    const std::string digits = std::to_string(y);
    out.append(digits.size() < 4 ? 4 - digits.size() : 0, '0');
    out += digits;
    out += '-' + two(static_cast<unsigned>(ymd.month())) + '-' +
           two(static_cast<unsigned>(ymd.day())) + 'T' + two(secs / 3600) + ':' +
           two((secs / 60) % 60) + ':' + two(secs % 60) + 'Z';
    return out;
}

[[nodiscard]] constexpr bool tz_abbreviation_char(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '+' || c == '-';
}

[[nodiscard]] constexpr bool tz_abbreviation(std::string_view s) noexcept
{
    return s.size() >= 3 && s.size() <= 6 && std::ranges::all_of(s, tz_abbreviation_char);
}

} // namespace detail
/// @endcond

/// @brief A time zone's rules: periods from a start instant, then a POSIX rule.
///
/// Immutable, copyable and safe to share between threads. Nothing is cached:
/// at() is a binary search, or three small rule evaluations after the last
/// period. A default-constructed zone is UTC.
class tz_rules
{
  public:
    /// @brief UTC (`fixed(0s, "UTC")`).
    tz_rules() : tz_rules{fixed(std::chrono::seconds{0}, "UTC")} {}

    /// @brief Parse a zone from its text form.
    ///
    /// The POSIX rule is `std offset [dst [offset] [,rule,rule]]`: names of 3
    /// to 6 letters, or `<...>` around 3 to 6 of `A-Z a-z 0-9 + -`; offsets
    /// `[+|-]hh[:mm[:ss]]` west of UTC (hours to 24; a daylight name without
    /// an offset is one hour ahead of standard); rules `Jn` (1 to 365,
    /// February 29 never counted), `n` (0 to 365, counting it) or `Mm.w.d`
    /// (weekday d, 0 = Sunday, of week w, 5 = last, of month m), each with an
    /// optional `/time` from -167 to 167 hours (default 02:00), local
    /// standard time for the start and local daylight time for the end. A
    /// daylight name needs the two rules. An empty rule means the last period
    /// holds forever.
    ///
    /// @param periods Periods in the text form of the file description.
    /// @param posix POSIX TZ rule, or empty.
    /// @return The zone.
    /// @throws std::invalid_argument With the position, for malformed text, no
    ///         period, starts that do not increase, a start outside the years
    ///         -9999 to 9999, an offset beyond ±86,399 seconds, or an
    ///         abbreviation outside the forms above.
    [[nodiscard]] static tz_rules parse(std::string_view periods, std::string_view posix) // NOLINT(bugprone-easily-swappable-parameters)
    {
        tz_rules z{no_init{}};
        z.parse_periods(periods);
        z._posix = std::string{posix};
        posix_reader reader{.zone = &z, .text = posix};
        reader.read();
        return z;
    }

    /// @brief A zone with one offset forever, for the `UTC+03:00` kind of zone.
    ///
    /// One period from the start of the year -9999, and the POSIX rule
    /// `<abbreviation>offset`.
    ///
    /// @param offset East of UTC, within ±86,399 seconds.
    /// @param abbreviation 3 to 6 of `A-Z a-z 0-9 + -` (`+03`, `UTC`).
    /// @return The zone.
    /// @throws std::invalid_argument For an offset or abbreviation outside those.
    [[nodiscard]] static tz_rules fixed(std::chrono::seconds offset, std::string_view abbreviation)
    {
        const std::string name{abbreviation};
        if (offset.count() > max_offset || offset.count() < -max_offset)
        {
            throw std::invalid_argument("tz_rules::fixed: offset " + std::to_string(offset.count()) +
                                        " s is beyond 86399 s");
        }
        if (!detail::tz_abbreviation(abbreviation))
        {
            throw std::invalid_argument("tz_rules::fixed: abbreviation '" + name +
                                        "' is not 3 to 6 of A-Z a-z 0-9 + -");
        }
        // POSIX offsets are west of UTC: +03:00 is "<+03>-3".
        const long long west = -offset.count();
        const long long magnitude = west < 0 ? -west : west;
        std::string posix = "<" + name + ">" + (west < 0 ? "-" : "") +
                            std::to_string(magnitude / 3600);
        if (magnitude % 3600 != 0)
        {
            const std::string clock = detail::iso_utc(
                std::chrono::sys_seconds{std::chrono::seconds{magnitude % 3600}});
            posix += ':' + clock.substr(14, 2); // minutes of "1970-01-01T00:mm:ssZ"
            if (magnitude % 60 != 0) posix += ':' + clock.substr(17, 2);
        }
        return parse(std::to_string(detail::tz_first.time_since_epoch().count()) + "/" +
                         std::to_string(offset.count()) + "/" + name + "/s",
                     posix);
    }

    /// @brief The rules in force at an instant.
    ///
    /// Before the last period's start, or with no daylight rule, the period
    /// that contains @p t. After it, the latest transition of the POSIX rule
    /// in the UTC years around @p t that is after the last period's start
    /// and not after @p t, or the last period if there is none. A transition
    /// of the rule never overrides the list.
    ///
    /// @param t Instant.
    /// @return Offset, daylight flag and abbreviation.
    /// @throws std::out_of_range Before valid_from() or after the year 9999.
    [[nodiscard]] tz_info at(std::chrono::sys_seconds t) const
    {
        check_range(t, "tz_rules::at");
        const period& last = _periods.back();
        if (!_footer.has_dst || t.time_since_epoch().count() < last.start)
        {
            const auto it = std::ranges::upper_bound(_periods, t.time_since_epoch().count(), {},
                                                     &period::start);
            return info(*(it - 1));
        }
        const int y = static_cast<int>(
            std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(t)}.year());
        bool found = false;
        change best{};
        for (int year = y - 1; year <= y + 1; ++year)
        {
            for (const change& c : changes(year))
            {
                if (c.at <= last.start || c.at > t.time_since_epoch().count()) continue;
                // At one instant (daylight time all year), daylight time wins.
                if (!found || c.at > best.at || (c.at == best.at && c.dst))
                {
                    best = c;
                    found = true;
                }
            }
        }
        return found ? footer_info(best.dst) : info(last);
    }

    /// @brief Local time at an instant.
    /// @param t Instant.
    /// @return `t + at(t).offset`, as a local time.
    /// @throws std::out_of_range As at().
    [[nodiscard]] std::chrono::local_seconds to_local(std::chrono::sys_seconds t) const
    {
        return std::chrono::local_seconds{t.time_since_epoch() + at(t).offset};
    }

    /// @brief The instant of a local time.
    ///
    /// With o1 and o2 the offsets in force one day before and after the local
    /// time read as UTC, the candidates are `l - o1` and `l - o2`; a candidate
    /// is valid when the offset at it is the one that made it. Two valid
    /// candidates are a repeated local time, resolved by @p choose; one is the
    /// answer; none means the local time was skipped, and the answer is
    /// `l - o1` (02:30 on a spring-forward night in Berlin is 01:30Z, which
    /// is 03:30 CEST). Assumes the offset changes at most once in two days.
    ///
    /// @param l Local time.
    /// @param choose Which instant a repeated local time gives.
    /// @return The instant.
    /// @throws std::out_of_range When the instant is outside the zone's data.
    [[nodiscard]] std::chrono::sys_seconds to_utc(std::chrono::local_seconds l,
                                                  tz_choose choose = tz_choose::earliest) const
    {
        constexpr std::chrono::days one_day{1};
        const std::chrono::sys_seconds as_utc{l.time_since_epoch()};
        if (as_utc < detail::tz_first - one_day || as_utc > detail::tz_last + one_day)
        {
            // Far outside any zone's data; also keeps the probes below from overflowing.
            check_range(as_utc, "tz_rules::to_utc");
        }
        const auto probe = [&](std::chrono::sys_seconds t) {
            return at(std::clamp(t, valid_from(), detail::tz_last)).offset;
        };
        const std::chrono::seconds o1 = probe(as_utc - one_day);
        const std::chrono::seconds o2 = probe(as_utc + one_day);
        const std::chrono::sys_seconds u1 = as_utc - o1;
        const std::chrono::sys_seconds u2 = as_utc - o2;
        const auto valid = [&](std::chrono::sys_seconds u, std::chrono::seconds o) {
            return u >= valid_from() && u <= detail::tz_last && at(u).offset == o;
        };
        const bool v1 = valid(u1, o1);
        const bool v2 = valid(u2, o2);
        if (v1 && v2 && u1 != u2)
        {
            return choose == tz_choose::earliest ? std::min(u1, u2) : std::max(u1, u2);
        }
        if (v2 && !v1) return u2;
        check_range(u1, "tz_rules::to_utc");
        return u1;
    }

    /// @brief The changes in [from, to), in order.
    ///
    /// Every listed period's start after the first, and every change of the
    /// POSIX rule after the last period that changes the offset or the
    /// daylight flag.
    ///
    /// @param from First instant (inclusive).
    /// @param to Last instant (exclusive).
    /// @return Each change's instant and the rules from it.
    [[nodiscard]] std::vector<std::pair<std::chrono::sys_seconds, tz_info>> transitions(
        std::chrono::sys_seconds from, std::chrono::sys_seconds to) const
    {
        std::vector<std::pair<std::chrono::sys_seconds, tz_info>> out;
        from = std::max(from, valid_from());
        to = std::min(to, detail::tz_last + std::chrono::seconds{1});
        if (from >= to) return out;
        for (std::size_t i = 1; i < _periods.size(); ++i)
        {
            const std::chrono::sys_seconds start{std::chrono::seconds{_periods[i].start}};
            if (start >= from && start < to) out.emplace_back(start, info(_periods[i]));
        }
        const period& last = _periods.back();
        const std::chrono::sys_seconds last_start{std::chrono::seconds{last.start}};
        if (!_footer.has_dst || to <= last_start) return out;

        const auto year_of = [](std::chrono::sys_seconds t) {
            return static_cast<int>(
                std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(t)}.year());
        };
        const std::chrono::sys_seconds begin = std::max(from, last_start);
        std::vector<change> found;
        for (int year = year_of(begin) - 1; year <= year_of(to - std::chrono::seconds{1}) + 1; ++year)
        {
            for (const change& c : changes(year))
            {
                if (c.at > last.start && c.at >= begin.time_since_epoch().count() &&
                    c.at < to.time_since_epoch().count())
                {
                    found.push_back(c);
                }
            }
        }
        std::ranges::sort(found, [](const change& a, const change& b) {
            return a.at < b.at || (a.at == b.at && !a.dst && b.dst);
        });
        tz_info previous = begin > last_start ? at(begin - std::chrono::seconds{1}) : info(last);
        for (std::size_t i = 0; i < found.size(); ++i)
        {
            // Of two changes at one instant (daylight time all year), the later counts.
            if (i + 1 < found.size() && found[i + 1].at == found[i].at) continue;
            const tz_info now = footer_info(found[i].dst);
            if (now.offset == previous.offset && now.dst == previous.dst) continue;
            out.emplace_back(std::chrono::sys_seconds{std::chrono::seconds{found[i].at}}, now);
            previous = now;
        }
        return out;
    }

    /// @brief The first instant the zone has data for: the first period's start.
    /// @return The instant.
    [[nodiscard]] std::chrono::sys_seconds valid_from() const noexcept
    {
        return std::chrono::sys_seconds{std::chrono::seconds{_periods.front().start}};
    }

    /// @brief Whether daylight time is in force at an instant or begins after it.
    /// @param t Instant.
    /// @return True when a daylight period ends after @p t, or the POSIX rule
    ///         has daylight time.
    [[nodiscard]] bool observes_dst_after(std::chrono::sys_seconds t) const noexcept
    {
        if (_footer.has_dst) return true;
        for (std::size_t i = 0; i < _periods.size(); ++i)
        {
            const bool ends_after = i + 1 == _periods.size() ||
                                    _periods[i + 1].start > t.time_since_epoch().count();
            if (_periods[i].dst && ends_after) return true;
        }
        return false;
    }

    /// @brief The periods in their text form.
    /// @return Text that parse() reads back to an equal zone.
    [[nodiscard]] std::string periods_text() const
    {
        std::string out;
        for (const period& p : _periods)
        {
            if (!out.empty()) out += ' ';
            out += std::to_string(p.start) + '/' + std::to_string(p.offset) + '/' +
                   std::string{abbreviation(p.abbr, p.abbr_size)} + '/' + (p.dst ? 'd' : 's');
        }
        return out;
    }

    /// @brief The POSIX rule, as parsed.
    /// @return The rule text (empty when there is none).
    [[nodiscard]] std::string_view posix() const noexcept { return _posix; }

    /// @brief Equal when the periods and the POSIX rule text are.
    /// @param a A zone.
    /// @param b Another.
    /// @return Whether they are equal.
    friend bool operator==(const tz_rules& a, const tz_rules& b) noexcept
    {
        return a._posix == b._posix &&
               std::ranges::equal(a._periods, b._periods, [&](const period& x, const period& y) {
                   return x.start == y.start && x.offset == y.offset && x.dst == y.dst &&
                          a.abbreviation(x.abbr, x.abbr_size) == b.abbreviation(y.abbr, y.abbr_size);
               });
    }

  private:
    static constexpr std::int32_t max_offset = 86399;

    struct no_init
    {
    };
    explicit tz_rules(no_init /*unused*/) {}

    struct period
    {
        std::int64_t start;  // UTC seconds
        std::int32_t offset; // east of UTC
        std::uint16_t abbr;  // position in _abbreviations
        std::uint8_t abbr_size;
        bool dst;
    };

    enum class date_kind : std::uint8_t
    {
        julian,  // Jn: 1-365, February 29 never counted
        ordinal, // n: 0-365, counting it
        weekday  // Mm.w.d
    };

    struct rule_date
    {
        date_kind kind{date_kind::ordinal};
        std::uint16_t day{0};
        std::uint8_t month{0};
        std::uint8_t week{0};
        std::uint8_t weekday{0};
        std::int32_t time{7200}; // seconds after local midnight
    };

    struct footer
    {
        bool has_dst{false};
        std::int32_t std_offset{0}; // east of UTC
        std::int32_t dst_offset{0};
        std::uint16_t std_abbr{0};
        std::uint8_t std_abbr_size{0};
        std::uint16_t dst_abbr{0};
        std::uint8_t dst_abbr_size{0};
        rule_date start;
        rule_date end;
    };

    struct change
    {
        std::int64_t at{0};
        bool dst{false};
    };

    // Reads the POSIX rule into a zone.
    struct posix_reader
    {
        tz_rules* zone;
        std::string_view text;
        std::size_t pos{0};

        [[noreturn]] static void fail(std::size_t at, const std::string& reason)
        {
            throw std::invalid_argument("tz_rules::parse: posix position " + std::to_string(at) +
                                        ": " + reason);
        }

        [[nodiscard]] bool done() const noexcept { return pos == text.size(); }

        void expect(char c)
        {
            if (done() || text[pos] != c) fail(pos, std::string{"expected '"} + c + "'");
            ++pos;
        }

        std::string_view name()
        {
            const std::size_t start = pos;
            if (!done() && text[pos] == '<')
            {
                const std::size_t close = text.find('>', pos);
                if (close == std::string_view::npos) fail(start, "unclosed '<'");
                const std::string_view inner = text.substr(pos + 1, close - pos - 1);
                if (!detail::tz_abbreviation(inner))
                {
                    fail(start, "a quoted abbreviation is 3 to 6 of A-Z a-z 0-9 + -");
                }
                pos = close + 1;
                return inner;
            }
            while (!done() && ((text[pos] >= 'A' && text[pos] <= 'Z') || (text[pos] >= 'a' && text[pos] <= 'z')))
            {
                ++pos;
            }
            if (pos - start < 3 || pos - start > 6) fail(start, "expected an abbreviation of 3 to 6 letters");
            return text.substr(start, pos - start);
        }

        std::int32_t number(std::size_t max_digits, std::int32_t max_value, const char* what) // NOLINT(bugprone-easily-swappable-parameters)
        {
            const std::size_t start = pos;
            std::int32_t value = 0;
            while (!done() && text[pos] >= '0' && text[pos] <= '9' && pos - start < max_digits)
            {
                value = (value * 10) + (text[pos] - '0');
                ++pos;
            }
            if (pos == start) fail(pos, std::string{"expected "} + what);
            if (value > max_value) fail(start, std::string{what} + " above " + std::to_string(max_value));
            return value;
        }

        // [+|-]h[h[h]][:mm[:ss]], in seconds.
        std::int32_t clock(std::int32_t max_hours)
        {
            std::int32_t sign = 1;
            if (!done() && (text[pos] == '+' || text[pos] == '-'))
            {
                sign = text[pos] == '-' ? -1 : 1;
                ++pos;
            }
            std::int32_t seconds = number(max_hours > 99 ? 3 : 2, max_hours, "hours") * 3600;
            if (!done() && text[pos] == ':')
            {
                ++pos;
                seconds += number(2, 59, "minutes") * 60;
                if (!done() && text[pos] == ':')
                {
                    ++pos;
                    seconds += number(2, 59, "seconds");
                }
            }
            return sign * seconds;
        }

        rule_date rule()
        {
            rule_date d;
            const std::size_t start = pos;
            if (!done() && text[pos] == 'J')
            {
                ++pos;
                d.kind = date_kind::julian;
                d.day = static_cast<std::uint16_t>(number(3, 365, "a day"));
                if (d.day == 0) fail(start, "a J day is 1 to 365");
            }
            else if (!done() && text[pos] == 'M')
            {
                ++pos;
                d.kind = date_kind::weekday;
                d.month = static_cast<std::uint8_t>(number(2, 12, "a month"));
                expect('.');
                d.week = static_cast<std::uint8_t>(number(1, 5, "a week"));
                expect('.');
                d.weekday = static_cast<std::uint8_t>(number(1, 6, "a weekday"));
                if (d.month == 0 || d.week == 0) fail(start, "months are 1 to 12 and weeks 1 to 5");
            }
            else if (!done() && text[pos] >= '0' && text[pos] <= '9')
            {
                d.kind = date_kind::ordinal;
                d.day = static_cast<std::uint16_t>(number(3, 365, "a day"));
            }
            else
            {
                fail(pos, "expected a rule date (Jn, n or Mm.w.d)");
            }
            if (!done() && text[pos] == '/')
            {
                ++pos;
                d.time = clock(167);
            }
            return d;
        }

        void read()
        {
            if (done()) return;
            footer& f = zone->_footer;
            const std::string_view std_name = name();
            f.std_offset = -clock(24);
            f.std_abbr = zone->add_abbreviation(std_name);
            f.std_abbr_size = static_cast<std::uint8_t>(std_name.size());
            if (done()) return;
            const std::string_view dst_name = name();
            f.dst_offset = !done() && text[pos] != ',' ? -clock(24) : f.std_offset + 3600;
            f.dst_abbr = zone->add_abbreviation(dst_name);
            f.dst_abbr_size = static_cast<std::uint8_t>(dst_name.size());
            if (done()) fail(pos, "daylight time needs its rules (,start,end)");
            expect(',');
            f.start = rule();
            expect(',');
            f.end = rule();
            if (!done()) fail(pos, "unexpected character");
            f.has_dst = true;
        }
    };

    [[noreturn]] static void period_error(std::size_t at, const std::string& reason)
    {
        throw std::invalid_argument("tz_rules::parse: periods position " + std::to_string(at) +
                                    ": " + reason);
    }

    template <class T>
    static T period_number(std::string_view text, std::size_t at, const char* what)
    {
        T value{};
        const char* first = text.data();
        const char* last = std::to_address(text.end());
        const auto [end, error] = std::from_chars(first, last, value);
        if (text.empty() || error != std::errc{} || end != last)
        {
            period_error(at, std::string{"expected "} + what);
        }
        return value;
    }

    // The four '/'-separated fields of the period at @p at.
    static std::array<std::string_view, 4> period_fields(std::string_view item, std::size_t at)
    {
        std::array<std::string_view, 4> parts{};
        std::size_t n = 0;
        std::size_t field = 0;
        for (std::size_t i = 0; i <= item.size(); ++i)
        {
            if (i == item.size() || item[i] == '/')
            {
                if (n == parts.size()) period_error(at, "a period has four fields");
                parts.at(n++) = item.substr(field, i - field);
                field = i + 1;
            }
        }
        if (n != parts.size()) period_error(at, "a period has four fields (start/offset/abbreviation/s|d)");
        return parts;
    }

    void parse_periods(std::string_view text)
    {
        if (text.empty()) period_error(0, "no period");
        std::size_t pos = 0;
        while (pos <= text.size())
        {
            const std::size_t space = std::min(text.find(' ', pos), text.size());
            const auto parts = period_fields(text.substr(pos, space - pos), pos);
            const auto start = period_number<std::int64_t>(parts[0], pos, "a start in seconds");
            if (start < detail::tz_first.time_since_epoch().count() ||
                start > detail::tz_last.time_since_epoch().count())
            {
                period_error(pos, "start outside the years -9999 to 9999");
            }
            if (!_periods.empty() && start <= _periods.back().start)
            {
                period_error(pos, "starts must increase");
            }
            const std::size_t offset_at = pos + parts[0].size() + 1;
            const auto offset = period_number<std::int32_t>(parts[1], offset_at, "an offset in seconds");
            if (offset > max_offset || offset < -max_offset)
            {
                period_error(offset_at, "offset beyond 86399 s");
            }
            const std::size_t abbr_at = offset_at + parts[1].size() + 1;
            if (!detail::tz_abbreviation(parts[2]))
            {
                period_error(abbr_at, "an abbreviation is 3 to 6 of A-Z a-z 0-9 + -");
            }
            if (parts[3] != "s" && parts[3] != "d")
            {
                period_error(abbr_at + parts[2].size() + 1, "expected s or d");
            }
            _periods.push_back({.start = start,
                                .offset = offset,
                                .abbr = add_abbreviation(parts[2]),
                                .abbr_size = static_cast<std::uint8_t>(parts[2].size()),
                                .dst = parts[3] == "d"});
            pos = space + 1;
        }
    }

    // Position of the text in _abbreviations, adding it when not already there.
    std::uint16_t add_abbreviation(std::string_view text)
    {
        std::size_t found = _abbreviations.find(text);
        if (found == std::string::npos)
        {
            found = _abbreviations.size();
            _abbreviations += text;
        }
        if (found > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::invalid_argument("tz_rules::parse: too many abbreviations");
        }
        return static_cast<std::uint16_t>(found);
    }

    [[nodiscard]] std::string_view abbreviation(std::uint16_t at, std::uint8_t size) const noexcept
    {
        return std::string_view{_abbreviations}.substr(at, size);
    }

    [[nodiscard]] tz_info info(const period& p) const noexcept
    {
        return {.offset = std::chrono::seconds{p.offset},
                .dst = p.dst,
                .abbreviation = abbreviation(p.abbr, p.abbr_size)};
    }

    [[nodiscard]] tz_info footer_info(bool dst) const noexcept
    {
        return dst ? tz_info{.offset = std::chrono::seconds{_footer.dst_offset},
                             .dst = true,
                             .abbreviation = abbreviation(_footer.dst_abbr, _footer.dst_abbr_size)}
                   : tz_info{.offset = std::chrono::seconds{_footer.std_offset},
                             .dst = false,
                             .abbreviation = abbreviation(_footer.std_abbr, _footer.std_abbr_size)};
    }

    void check_range(std::chrono::sys_seconds t, const char* function) const
    {
        if (t < valid_from())
        {
            throw std::out_of_range(std::string{function} + ": " + detail::iso_utc(t) +
                                    " is before the zone's data, which starts at " +
                                    detail::iso_utc(valid_from()));
        }
        if (t > detail::tz_last)
        {
            throw std::out_of_range(std::string{function} + ": " + detail::iso_utc(t) +
                                    " is after the last supported instant, " +
                                    detail::iso_utc(detail::tz_last));
        }
    }

    // The day a rule date falls on in a year.
    [[nodiscard]] static std::chrono::sys_days date(const rule_date& d, int y) noexcept
    {
        const std::chrono::year year{y};
        const std::chrono::sys_days january{year / 1 / 1};
        switch (d.kind)
        {
        case date_kind::julian:
            return january + std::chrono::days{d.day - 1 + (year.is_leap() && d.day >= 60 ? 1 : 0)};
        case date_kind::ordinal: return january + std::chrono::days{d.day};
        case date_kind::weekday: break;
        }
        const std::chrono::month month{d.month};
        const std::chrono::sys_days first{year / month / 1};
        const std::chrono::sys_days last{year / month / std::chrono::last};
        std::chrono::sys_days day = first +
                                    (std::chrono::weekday{d.weekday} - std::chrono::weekday{first}) +
                                    std::chrono::days{7 * (d.week - 1)};
        while (day > last) day -= std::chrono::days{7};
        return day;
    }

    // The rule's two changes in a year: to daylight time, then to standard.
    [[nodiscard]] std::array<change, 2> changes(int year) const noexcept
    {
        const auto at = [&](const rule_date& d, std::int32_t offset) {
            return (std::chrono::sys_seconds{date(d, year)} +
                    std::chrono::seconds{d.time - offset})
                .time_since_epoch()
                .count();
        };
        return {change{.at = at(_footer.start, _footer.std_offset), .dst = true},
                change{.at = at(_footer.end, _footer.dst_offset), .dst = false}};
    }

    std::vector<period> _periods;
    std::string _abbreviations;
    std::string _posix;
    footer _footer;
};

} // namespace dasmig::core

#endif // DASMIG_CORE_TZ_HPP
