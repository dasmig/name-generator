// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_NAMEGEN_HPP
#define DASMIG_NAMEGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#ifdef DASMIG_NAME_EMBED
#include "namegen_data.hpp"
#endif

/// @file namegen.hpp
/// @brief Name generator library — culture-aware name generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 1);

struct ng_test_access;

namespace dasmig
{

/// @brief Culture representing a country or a broader group.
enum class culture : std::uint8_t
{
    afghan,
    albanian,
    algerian,
    american,
    angolan,
    argentinian,
    austrian,
    azerbaijani,
    bahraini,
    bangladeshi,
    belgian,
    bolivian,
    botswanan,
    brazilian,
    british,
    bruneian,
    bulgarian,
    burkinabe,
    burundian,
    cambodian,
    cameroonian,
    canadian,
    chilean,
    chinese,
    colombian,
    costarican,
    croatian,
    cypriot,
    czech,
    danish,
    djiboutian,
    dutch,
    ecuadorian,
    egyptian,
    emirati,
    estonian,
    ethiopian,
    fijian,
    filipino,
    finnish,
    french,
    georgian,
    german,
    ghanaian,
    greek,
    guatemalan,
    haitian,
    honduran,
    hongkonger,
    hungarian,
    icelandic,
    indian,
    indonesian,
    iranian,
    iraqi,
    irish,
    israeli,
    italian,
    jamaican,
    japanese,
    jordanian,
    kazakh,
    korean,
    kuwaiti,
    lebanese,
    libyan,
    lithuanian,
    luxembourgish,
    macanese,
    malaysian,
    maldivian,
    maltese,
    mauritian,
    mexican,
    moldovan,
    moroccan,
    namibian,
    nigerian,
    norwegian,
    omani,
    palestinian,
    panamanian,
    peruvian,
    polish,
    portuguese,
    puertorican,
    qatari,
    russian,
    salvadoran,
    saudi,
    serbian,
    singaporean,
    slovenian,
    southafrican,
    spanish,
    sudanese,
    swedish,
    swiss,
    syrian,
    taiwanese,
    tunisian,
    turkish,
    turkmen,
    uruguayan,
    yemeni,
    any
};

/// @brief Simple gender enum to distinguish between male and female names.
enum class gender : std::uint8_t
{
    m,
    f,
    any
};

/// @brief The per-call choices of a generation: gender and culture.
///
/// Passed to ng::get_name() and ng::get_surname(). Both default to `any`
/// (random). For a surname the gender selects a gendered surname list where
/// the culture has one.
///
/// @code
/// gen.get_name({.gender = gender::f, .culture = culture::french});
/// @endcode
struct name_request
{
    dasmig::gender gender = dasmig::gender::any;    ///< Requested gender.
    dasmig::culture culture = dasmig::culture::any; ///< Requested culture.
};

/// @brief The order a culture writes the given and the family name in.
enum class name_order : std::uint8_t
{
    given_family, ///< `John Smith`.
    family_given  ///< `Kim Minjun`.
};

/// @brief How a culture forms a patronymic from the father's given name.
enum class patronymic_style : std::uint8_t
{
    none,
    east_slavic, ///< `Ivan` gives `Ivanovich` or `Ivanovna`.
    icelandic    ///< `Jón` gives `Jónsson` or `Jónsdóttir`.
};

/// @brief Which parents' surnames a child of a two-surname culture takes,
///        and in what order.
enum class surname_order : std::uint8_t
{
    paternal_first, ///< The father's first surname, then the mother's first (Spain).
    maternal_first  ///< The mother's last surname, then the father's last (Portugal).
};

/// @brief How a culture composes a full name; see ng::convention().
///
/// A culture without a row in `conventions.tsv` has the default: one
/// surname, given name first.
struct naming_convention
{
    name_order order = name_order::given_family;               ///< Given or family name first.
    patronymic_style patronymic = patronymic_style::none;      ///< How the patronymic is formed.
    bool family_name = true;      ///< False when the patronymic replaces the family name.
    int surnames = 1;             ///< 1, or 2 for cultures with a double surname.
    double second_surname = 1.0;  ///< Chance of the second surname when `surnames` is 2.
    surname_order inheritance = surname_order::paternal_first; ///< Which parents' surnames a child takes.
};

/// @brief The part of a full name a word is.
enum class name_role : std::uint8_t
{
    given,
    patronymic,
    family,
    family2 ///< The second surname of a double surname.
};

/// @brief The per-call choices of ng::get_child_name().
struct child_name_request
{
    dasmig::gender gender = dasmig::gender::any; ///< The child's gender; `any` draws one.
};

class ng;

/// @brief Return type for name generation, holding both individual parts and
/// the full composed string.
///
/// Supports implicit conversion to std::wstring, streaming via operator<<,
/// chained appending of names and surnames, and JSON output via to_json().
class name
{
  public:
    /// @brief Retrieve the random seed used to generate this name.
    /// @return The per-call seed for replay.
    /// @see ng::get_name(const name_request&, std::uint64_t)
    [[nodiscard]] std::uint64_t seed() const
    {
        return _signature.seed;
    }

    /// @brief The seed and the data this name was generated from.
    /// @return The replay signature; `data` is ng::data_hash() at the time
    ///         of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Return the individual parts (names/surnames) as a vector.
    /// @return Vector of name parts.
    [[nodiscard]] const std::vector<std::wstring>& parts() const
    {
        return _parts;
    }

    /// @brief The role of each part, in the order of parts().
    /// @return One role per part; `given` for a forename, `family` for a surname.
    [[nodiscard]] const std::vector<name_role>& roles() const
    {
        return _roles;
    }

    /// @brief Whether this name was composed by a culture's convention
    ///        (ng::get_full_name(), ng::get_child_name() or ng::get_married_name()).
    [[nodiscard]] bool composed() const
    {
        return _composed;
    }

    /// @brief The given name (empty when the name has none).
    [[nodiscard]] std::wstring given() const
    {
        return joined(name_role::given, name_role::given);
    }

    /// @brief The patronymic (empty when the culture has none).
    [[nodiscard]] std::wstring patronymic() const
    {
        return joined(name_role::patronymic, name_role::patronymic);
    }

    /// @brief The family name; both surnames of a double surname, in order.
    [[nodiscard]] std::wstring family() const
    {
        return joined(name_role::family, name_role::family2);
    }

    /// @brief Append a forename to this name, preserving gender and culture.
    ///
    /// The part is drawn from this name's seed and its position, so the same
    /// seed and the same appends give the same full name.
    /// @return `*this` for chaining.
    /// @throws std::invalid_argument If no names are loaded for the culture
    ///         and gender.
    name& append_name();

    /// @brief Append a forename of a specific culture.
    /// @param c Culture for the appended name.
    /// @return `*this` for chaining.
    /// @throws std::invalid_argument If no names are loaded for @p c.
    name& append_name(culture c);

    /// @brief Append a surname to this name, preserving culture.
    /// @return `*this` for chaining.
    /// @throws std::invalid_argument If no surnames are loaded for the
    ///         culture.
    name& append_surname();

    /// @brief Append a surname of a specific culture.
    /// @param c Culture for the appended surname.
    /// @return `*this` for chaining.
    /// @throws std::invalid_argument If no surnames are loaded for @p c.
    name& append_surname(culture c);

    /// @brief Write this name as a JSON object
    ///        `{"name":…,"parts":[…],"gender":…,"culture":…,"signature":{…}}`.
    ///
    /// `gender` is the gender of the forenames (for a surname, the one
    /// append_name() would use) and `culture` the culture of the first part.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const;

    /// @brief This name as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::wstring.
    /// @return The full composed name string.
    operator std::wstring() const // NOLINT(hicpp-explicit-conversions)
    {
        return _full_string;
    }

    /// @brief Implicit conversion to a vector of name parts.
    operator std::vector<std::wstring>() const // NOLINT(hicpp-explicit-conversions)
    {
        return _parts;
    }

    /// @brief Stream the name to a wide output stream.
    /// @param wos Output stream.
    /// @param n Name to stream.
    /// @return Reference to the output stream.
    friend std::wostream& operator<<(std::wostream& wos, const name& n)
    {
        wos << n._full_string;
        return wos;
    }

  private:
    // Private constructor — names are created only by ng.
    name(std::wstring name_str, gender g, culture c, const ng* owner,
         name_role role = name_role::given)
        : _full_string(std::move(name_str)), _gender(g), _culture(c), _owner(owner)
    {
        _parts.push_back(_full_string);
        _roles.push_back(role);
    }

    void append_part(std::wstring part, name_role role)
    {
        _full_string.append(L" ").append(part);
        _parts.push_back(std::move(part));
        _roles.push_back(role);
    }

    // The parts whose role lies in [first, last], joined by spaces.
    [[nodiscard]] std::wstring joined(name_role first, name_role last) const
    {
        std::wstring out;
        for (std::size_t i = 0; i < _parts.size(); ++i)
        {
            if (_roles.at(i) < first || _roles.at(i) > last) continue;
            if (!out.empty()) out.append(L" ");
            out.append(_parts.at(i));
        }
        return out;
    }

    std::wstring _full_string;        ///< Full composed name.
    std::vector<std::wstring> _parts; ///< Individual name parts.
    std::vector<name_role> _roles;    ///< The role of each part.
    bool _composed = false;           ///< Composed by a convention.
    gender _gender;                   ///< Gender of the forenames.
    culture _culture;                 ///< Culture of the first part.
    const ng* _owner;                 ///< Generator that created this name.
    core::seed_signature _signature;  ///< Seed and data hash.

    friend class ng;
};

/// @brief Name generator that produces culture-aware names and surnames.
///
/// Generates realistic names by picking from popular name databases indexed
/// by culture and gender, weighted by popularity. Supports 105 cultures.
///
/// Output is identical on every compiler and platform (dasmig-core): a seed,
/// the same request and the same data give the same name everywhere.
///
/// Can be used as a singleton via instance() or constructed independently.
/// Independent instances own their own name databases and random engine.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_name() on
/// the **same** instance require external synchronization. load() and
/// extend() mutate internal state and must not be called concurrently with
/// get_name() on the same instance.
class ng
{
  public:
    /// @brief Default constructor — creates an empty generator with no names.
    ///
    /// Call load() to populate name databases before generating.
    ng() = default;

    ng(const ng&) = delete;            ///< Not copyable.
    ng& operator=(const ng&) = delete; ///< Not copyable.
    ng(ng&&) noexcept = default;            ///< Move constructor.
    ng& operator=(ng&&) noexcept = default; ///< Move assignment.
    ~ng() = default;                   ///< Default destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier on first access (or `full` when `lite` is not
    /// found), from compiled-in data or an auto-probed `resources/`.
    /// For independent generators, prefer constructing a separate ng instance.
    /// @return Reference to the global ng instance.
    static ng& instance()
    {
        static ng inst{auto_probe_tag{}};
        return inst;
    }

    /// @brief Translate an ISO 3166 2-letter country code to a culture enum.
    /// @param country_code Two-letter country code (e.g., L"us", L"br").
    /// @return Matching culture, or culture::any if not recognized.
    [[nodiscard]] static culture to_culture(std::wstring_view country_code)
    {
        const auto code = core::to_utf8(std::wstring{country_code});
        for (std::size_t i = 0; i < culture_codes_.size(); ++i)
        {
            if (culture_codes_.at(i) == code) return static_cast<culture>(i);
        }
        return culture::any;
    }

    /// @brief Translate a gender string to a gender enum.
    /// @param gender_string Gender string (e.g., L"male", L"female", L"m", L"f").
    /// @return Matching gender, or gender::any if not recognized.
    [[nodiscard]] static gender to_gender(std::wstring_view gender_string)
    {
        if (gender_string == L"m" || gender_string == L"male") return gender::m;
        if (gender_string == L"f" || gender_string == L"female") return gender::f;
        return gender::any;
    }

    /// @brief The ISO 3166 2-letter country code of a culture.
    /// @param c Culture.
    /// @return Lower-case code such as `"br"`, or `""` for culture::any.
    [[nodiscard]] static std::string_view culture_code(culture c)
    {
        const auto idx = static_cast<std::size_t>(c);
        return idx < culture_codes_.size() ? culture_codes_.at(idx) : std::string_view{};
    }

    /// @brief The name of a culture.
    /// @param c Culture.
    /// @return Lower-case name such as `"brazilian"`, or `"any"`.
    [[nodiscard]] static std::string_view culture_name(culture c)
    {
        const auto idx = static_cast<std::size_t>(c);
        return idx < culture_names_.size() ? culture_names_.at(idx) : std::string_view{"any"};
    }

    /// @brief Generate a first name.
    /// @param request Gender and culture (both default to `any`).
    /// @return A name object supporting chained appending.
    /// @throws std::invalid_argument If no names loaded for the resolved culture/gender.
    [[nodiscard]] name get_name(const name_request& request)
    {
        return get_name(request, _engine());
    }

    /// @brief Generate a deterministic first name using a specific seed.
    /// @param request Gender and culture.
    /// @param call_seed Seed; the same seed, request and data give the same
    ///        name on every platform.
    /// @return A name object.
    /// @throws std::invalid_argument If no names loaded for the resolved culture/gender.
    [[nodiscard]] name get_name(const name_request& request,
                                std::uint64_t call_seed) const
    {
        return solver(true, request.gender, request.culture, call_seed);
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of name results from a seed drawn like
    ///        get_name() draws its own.
    ///
    /// Item `i` is `get_name(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<name> get_name_batch(const name_request& request,
                                                  const core::batch_options& options)
    {
        return get_name_batch(request, options, _engine());
    }

    /// @brief Generate a batch of name results, deterministically.
    ///
    /// Item `i` is exactly `get_name(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `name`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<name> get_name_batch(const name_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_name(request, item_seed); });
    }

    /// @brief Stream a batch of name results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_name_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_name(const name_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_name(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of name results with a batch seed drawn like
    ///        get_name() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_name(const name_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_name(request, options, _engine(), std::forward<Sink>(sink));
    }


    /// @brief Generate a surname.
    /// @param request Culture, and gender for cultures with gendered surnames
    ///        (both default to `any`).
    /// @return A name object supporting chained appending.
    /// @throws std::invalid_argument If no surnames loaded for the resolved culture.
    [[nodiscard]] name get_surname(const name_request& request)
    {
        return get_surname(request, _engine());
    }

    /// @brief Generate a deterministic surname using a specific seed.
    /// @param request Culture and gender.
    /// @param call_seed Seed for reproducible results.
    /// @return A name object.
    /// @throws std::invalid_argument If no surnames loaded for the resolved culture.
    [[nodiscard]] name get_surname(const name_request& request,
                                   std::uint64_t call_seed) const
    {
        return solver(false, request.gender, request.culture, call_seed);
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of surname results from a seed drawn like
    ///        get_surname() draws its own.
    ///
    /// Item `i` is `get_surname(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<name> get_surname_batch(const name_request& request,
                                                  const core::batch_options& options)
    {
        return get_surname_batch(request, options, _engine());
    }

    /// @brief Generate a batch of surname results, deterministically.
    ///
    /// Item `i` is exactly `get_surname(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `name`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<name> get_surname_batch(const name_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_surname(request, item_seed); });
    }

    /// @brief Stream a batch of surname results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_surname_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_surname(const name_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_surname(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of surname results with a batch seed drawn like
    ///        get_surname() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_surname(const name_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_surname(request, options, _engine(), std::forward<Sink>(sink));
    }


    // -- Full names, children and marriage ------------------------------------------

    /// @brief How a culture composes a full name.
    /// @param c Culture.
    /// @return The convention loaded from `conventions.tsv`; the default (one
    ///         surname, given name first) for a culture without a row.
    [[nodiscard]] naming_convention convention(culture c) const
    {
        const auto it = _conventions.find(c);
        return it == _conventions.end() ? naming_convention{} : it->second;
    }

    /// @brief Generate a full name by the culture's convention.
    ///
    /// The parts follow the culture's order: family name first for Chinese,
    /// Japanese and Korean; two surnames for Spanish and Portuguese; a
    /// patronymic from a drawn father's name for Russian and Icelandic. The
    /// parts carry roles (name::roles()). get_name() and get_surname() are
    /// unchanged by conventions.
    ///
    /// @param request Gender and culture (both default to `any`).
    /// @return A composed name.
    /// @throws std::invalid_argument If no names are loaded for the resolved
    ///         culture and gender.
    [[nodiscard]] name get_full_name(const name_request& request)
    {
        return get_full_name(request, _engine());
    }

    /// @brief Generate a deterministic full name.
    /// @param request Gender and culture.
    /// @param call_seed Seed; the same seed, request and data give the same
    ///        name on every platform.
    /// @return A composed name.
    /// @throws std::invalid_argument If no names are loaded for the resolved
    ///         culture and gender.
    [[nodiscard]] name get_full_name(const name_request& request, std::uint64_t call_seed) const
    {
        const core::rng r{call_seed};
        auto rg = r.split("gender");
        const gender g = resolve_gender(request.gender, rg);
        auto rc = r.split("culture");
        const culture c = resolve_composed_culture_(request.culture, g == gender::m, g == gender::f, rc);
        return make_name_(draw_person_(g, c, r.split("person")), c, call_seed);
    }

    /// @brief Generate a batch of full names from a seed drawn like
    ///        get_full_name() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<name> get_full_name_batch(const name_request& request,
                                                        const core::batch_options& options)
    {
        return get_full_name_batch(request, options, _engine());
    }

    /// @brief Generate a batch of full names, deterministically: item `i` is
    ///        `get_full_name(request, core::item_seed(batch_seed, options.first + i))`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<name> get_full_name_batch(const name_request& request,
                                                        const core::batch_options& options,
                                                        std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_full_name(request, item_seed); });
    }

    /// @brief Stream a batch of full names to a sink, in index order.
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_full_name(const name_request& request, const core::batch_options& options,
                            std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_full_name(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of full names with a batch seed drawn like
    ///        get_full_name() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, name&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_full_name(const name_request& request, const core::batch_options& options,
                            Sink&& sink)
    {
        for_each_full_name(request, options, _engine(), std::forward<Sink>(sink));
    }

    /// @brief Name a child of two people by the culture's convention.
    ///
    /// The child takes its family name from the parents as the culture
    /// does: one surname cultures give the father's family name (a
    /// daughter its female form where the culture has one); two-surname
    /// cultures give one surname of each parent, in the culture's
    /// surname_order; patronymic cultures derive the patronymic from the
    /// father's given name. The given name is drawn from the father's
    /// culture. Only the parts' roles are read, so the parents can be any
    /// names (composed or not); the structure of a family (who the
    /// parents are, how many children) is the caller's.
    ///
    /// Siblings are not made distinct: draw each with its own seed and
    /// compare, if that matters.
    ///
    /// @param father The father's name; its culture is the child's.
    /// @param mother The mother's name.
    /// @param request The child's gender.
    /// @return A composed name.
    /// @throws std::invalid_argument If the father's culture is `any` or has
    ///         no forenames or surnames loaded.
    [[nodiscard]] name get_child_name(const name& father, const name& mother,
                                      const child_name_request& request)
    {
        return get_child_name(father, mother, request, _engine());
    }

    /// @brief Name a child of two people, deterministically.
    /// @param father The father's name; its culture is the child's.
    /// @param mother The mother's name.
    /// @param request The child's gender.
    /// @param call_seed Seed; the same seed, parents and data give the same
    ///        name on every platform.
    /// @return A composed name.
    /// @throws std::invalid_argument If the father's culture is `any` or has
    ///         no forenames or surnames loaded.
    [[nodiscard]] name get_child_name(const name& father, const name& mother,
                                      const child_name_request& request,
                                      std::uint64_t call_seed) const
    {
        const culture c = father._culture;
        if (c == culture::any) throw std::invalid_argument("get_child_name: the father has no culture");
        const core::rng r{call_seed};
        auto rg = r.split("gender");
        const gender g = resolve_gender(request.gender, rg);
        const auto cv = convention(c);
        const person dad = person_of_(father);
        const person mum = person_of_(mother);

        person child{.g = g, .given = pick_forename_(g, c, r.split("given")), .patronymic = {}, .family = {}};
        if (cv.patronymic != patronymic_style::none)
        {
            child.patronymic = patronymic_of_(cv.patronymic, dad.given, g == gender::f);
        }
        child.family = child_family_(dad, mum, g, c, cv);
        return make_name_(child, c, call_seed);
    }

    /// @brief The name a woman has after marrying, by the culture's convention.
    ///
    /// A single-surname culture gives her the husband's family name, in its
    /// married form where the culture has one (Kozlov gives Kozlova); a
    /// two-surname or patronymic culture leaves her name as it is. Only the
    /// roles of the names are read.
    ///
    /// @param wife The woman's name.
    /// @param husband The husband's name; its culture is the one used.
    /// @return A composed name with the wife's seed.
    [[nodiscard]] name get_married_name(const name& wife, const name& husband) const
    {
        const culture c = husband._culture;
        person p = person_of_(wife);
        const person other = person_of_(husband);
        const auto cv = convention(c);
        if (c != culture::any && cv.family_name && cv.surnames == 1 && !other.family.empty())
        {
            p.family = {feminize_(other.family.front(), c, true)};
        }
        return make_name_(p, wife._culture, wife.seed());
    }

    /// @brief Name a parent of an existing person, consistently with the
    ///        person's name; the inverse of get_child_name().
    ///
    /// The father's family name is the child's (in its male form where the
    /// culture has one), and in a patronymic culture his given name is one
    /// whose patronymic is the child's. Two-surname cultures give the
    /// surname the child took from this parent, in the slot the culture
    /// puts it. Everything the child's name does not fix (the mother's
    /// family name, the other surname of a parent, a grandparent's name in
    /// a patronymic) is drawn. The culture is the child's. Only the roles
    /// of the child's parts are read.
    ///
    /// @param child The person's name; its culture is the parent's.
    /// @param parent_gender `gender::m` or `gender::f`.
    /// @return A composed name.
    /// @throws std::invalid_argument If the child's culture is `any`, the
    ///         gender is `any`, or the culture has no names loaded.
    [[nodiscard]] name get_parent_name(const name& child, gender parent_gender)
    {
        return get_parent_name(child, parent_gender, _engine());
    }

    /// @brief Name a parent of an existing person, deterministically.
    /// @param child The person's name; its culture is the parent's.
    /// @param parent_gender `gender::m` or `gender::f`.
    /// @param call_seed Seed; the same seed, child and data give the same
    ///        name on every platform.
    /// @return A composed name.
    /// @throws std::invalid_argument If the child's culture is `any`, the
    ///         gender is `any`, or the culture has no names loaded.
    [[nodiscard]] name get_parent_name(const name& child, gender parent_gender,
                                       std::uint64_t call_seed) const
    {
        const culture c = child._culture;
        if (c == culture::any) throw std::invalid_argument("get_parent_name: the child has no culture");
        if (parent_gender != gender::m && parent_gender != gender::f)
        {
            throw std::invalid_argument("get_parent_name: the parent's gender must be male or female");
        }
        const core::rng r{call_seed};
        const auto cv = convention(c);
        const person kid = person_of_(child);
        const bool father = parent_gender == gender::m;
        person p = draw_person_(parent_gender, c, r.split("person"));

        if (father && cv.patronymic != patronymic_style::none && !kid.patronymic.empty())
        {
            p.given = father_given_(cv.patronymic, kid, c, p.given, r.split("given"));
        }
        if (cv.family_name && !kid.family.empty())
        {
            if (cv.surnames == 2 && kid.family.size() == 2)
            {
                // The inverse of child_family_(): which of the child's surnames
                // came from this parent, and the slot it sits in.
                const bool paternal = cv.inheritance == surname_order::paternal_first;
                const auto& inherited = kid.family.at(father == paternal ? 0 : 1);
                if (p.family.size() < 2) p.family.push_back(pick_surname_(parent_gender, c, r.split("family2")));
                p.family.at(paternal ? 0 : 1) = inherited;
            }
            else if (father)
            {
                p.family = {masculine_(kid.family.back(), c)};
            }
            else if (cv.surnames == 2)
            {
                p.family.resize(1); // a child with one surname had parents with one
            }
        }
        return make_name_(p, c, call_seed);
    }

    /// @brief The patronymic a father's given name gives a child.
    ///
    /// Rule-based, for names in Latin script, with a short list of irregular
    /// names (Pavel to Pavlovich, Jón to Jónsson).
    /// @param style The culture's patronymic style.
    /// @param father The father's given name.
    /// @param daughter Whether the child is a daughter.
    /// @return The patronymic; empty for patronymic_style::none.
    [[nodiscard]] static std::wstring patronymic_of(patronymic_style style, const std::wstring& father,
                                                    bool daughter)
    {
        return patronymic_of_(style, father, daughter);
    }

    /// @brief The female form of a male surname, by the culture's
    ///        `surname_forms.tsv` rules.
    /// @param surname A male surname.
    /// @param c Culture.
    /// @param married Whether to give the form a wife takes rather than the
    ///        one a daughter has (they differ in Lithuanian).
    /// @return The form; @p surname when the culture has no rule for it.
    [[nodiscard]] std::wstring female_form(const std::wstring& surname, culture c,
                                           bool married = false) const
    {
        return feminize_(surname, c, married);
    }

    /// @brief The layers in effect for `conventions.tsv`.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& convention_layers() const
    {
        return _convention_table.layers();
    }

    /// @brief The layers in effect for `surname_forms.tsv`.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& surname_form_layers() const
    {
        return _form_table.layers();
    }

    /// @brief Check whether any name databases have been loaded.
    /// @return `true` if at least one name list is loaded.
    [[nodiscard]] bool has_resources() const
    {
        return !_m_pool.empty() || !_f_pool.empty() || !_sur_pool.empty();
    }

    /// @brief Load name files from a directory, replacing all names and
    ///        layers loaded before.
    ///
    /// Reads every `.names` file below @p resource_path, in path order (see
    /// doc/usage.md for the format). A path that is not a directory is
    /// ignored.
    ///
    /// @param resource_path Directory containing `.names` files, such as
    ///        `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the names are then
    ///         unchanged.
    void load(const std::filesystem::path& resource_path)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(resource_path, ec)) return;
        apply_(read_directory_(resource_path), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all names and layers loaded
    ///        before.
    ///
    /// `lite` uses the compiled-in copy when built with `DASMIG_NAME_EMBED`.
    /// Otherwise the tier is read from the first of `resources`,
    /// `../resources`, `name-generator/resources` and
    /// `../name-generator/resources` that has it.
    ///
    /// @param tier The dataset size to load (dataset::lite or dataset::full).
    /// @return `true` if the tier was found and loaded.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        data_bundle bundle;
        for (const auto& file : embedded_files_())
        {
            if (!file.path.starts_with(tier_dir + "/")) continue;
            const std::string source = "embedded:" + std::string{file.path};
            if (file.path.ends_with(".names"))
            {
                bundle.names.push_back(parse_names_(file.text(), source));
            }
            else if (file.path == tier_dir + "/" + std::string{conventions_file_})
            {
                bundle.conventions = core::tsv_document::parse(file.text(), source);
            }
            else if (file.path == tier_dir + "/" + std::string{forms_file_})
            {
                bundle.forms = core::tsv_document::parse(file.text(), source);
            }
        }
        if (bundle.names.empty())
        {
            const auto root = core::find_resources(library_name_, tier_dir + "/" + std::string{marker_file_});
            if (!root) return false;
            bundle = read_directory_(*root / tier_dir);
        }
        if (bundle.names.empty()) return false;
        apply_(std::move(bundle), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add name layers from a `.names` file or a directory of them.
    ///
    /// Each file names its culture and gender in its first two lines, so it
    /// adds to (or changes, or removes from) that list. A directory is
    /// applied all together, or not at all if any file is invalid.
    ///
    /// @param path A `.names` file or a directory.
    /// @param mode How the layers combine with the names loaded before.
    /// @throws core::tsv_error If a file cannot be read or is invalid; the
    ///         names are then unchanged.
    /// @throws std::invalid_argument If @p path is a file without the
    ///         `.names` extension.
    void extend(const std::filesystem::path& path,
                core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        if (path.filename() == conventions_file_ || path.filename() == forms_file_)
        {
            data_bundle bundle;
            auto doc = core::tsv_document::parse(read_text_(path), path.string());
            (path.filename() == conventions_file_ ? bundle.conventions : bundle.forms) = std::move(doc);
            apply_(std::move(bundle), mode, false);
            return;
        }
        if (path.extension() != ".names")
        {
            throw std::invalid_argument("extend: expected a .names, conventions.tsv or "
                                        "surname_forms.tsv file, got " +
                                        path.filename().string());
        }
        data_bundle bundle;
        bundle.names.push_back(read_file_(path));
        apply_(std::move(bundle), mode, false);
    }

    /// @brief Add a layer of `conventions.tsv` from text.
    /// @param text Tab-separated text with a header line: `culture` (a
    ///        culture name such as `russian`, the key) and any of `order`
    ///        (`given_family`, `family_given`), `patronymic` (`none`,
    ///        `east_slavic`, `icelandic`), `family_name` (0 or 1),
    ///        `surnames` (1 or 2), `second_surname` (a chance from 0 to 1)
    ///        and `surname_order` (`paternal_first`, `maternal_first`).
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the conventions loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If the text is invalid; nothing then changes.
    const core::layer_info& extend_conventions_from_string(
        std::string_view text, std::string source,
        core::layer_mode mode = core::layer_mode::override)
    {
        data_bundle bundle;
        bundle.conventions = core::tsv_document::parse(text, std::move(source));
        apply_(std::move(bundle), mode, false);
        return _convention_table.layers().back();
    }

    /// @brief Add a layer of `surname_forms.tsv` from text.
    /// @param text Tab-separated text with a header line: `culture` and
    ///        `male` (the keys: the ending of a male surname, or `*` for
    ///        any), `female` (the ending a daughter has instead) and
    ///        optionally `married` (the ending a wife has; `female` when
    ///        empty). The longest matching `male` ending applies.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the rules loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If the text is invalid; nothing then changes.
    const core::layer_info& extend_surname_forms_from_string(
        std::string_view text, std::string source,
        core::layer_mode mode = core::layer_mode::override)
    {
        data_bundle bundle;
        bundle.forms = core::tsv_document::parse(text, std::move(source));
        apply_(std::move(bundle), mode, false);
        return _form_table.layers().back();
    }

    /// @brief Add a name layer from text in the `.names` format.
    /// @param text The country code, the list (`male`, `female` or `any`
    ///        for surnames; `male-surname` or `female-surname` for the
    ///        surnames of one gender), then one name per line with an
    ///        optional weight.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the names loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If the text is invalid; the names are then
    ///         unchanged.
    const core::layer_info& extend_from_string(std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        data_bundle bundle;
        bundle.names.push_back(parse_names_(std::string{text}, std::move(source)));
        const auto list = bundle.names.front().list;
        const auto c = bundle.names.front().c;
        apply_(std::move(bundle), mode, false);
        return _tables.at(list).at(c).layers().back();
    }

    /// @brief The layers in effect for one name list.
    /// @param c Culture.
    /// @param g gender::m or gender::f for forenames, gender::any for
    ///        surnames.
    /// @return The layer records.
    /// @throws std::out_of_range If that list was never loaded.
    [[nodiscard]] const std::vector<core::layer_info>& layers(culture c, gender g) const
    {
        return _tables.at(list_of_(g)).at(c).layers();
    }

    /// @brief The layers in effect for the surnames of one gender.
    /// @param c Culture.
    /// @param g gender::m or gender::f; gender::any is the surname list
    ///        shared by both.
    /// @return The layer records.
    /// @throws std::out_of_range If that list was never loaded (a culture
    ///         has one only when its surnames change with gender).
    [[nodiscard]] const std::vector<core::layer_info>& surname_layers(culture c, gender g) const
    {
        const auto list = g == gender::m   ? male_surname_list
                          : g == gender::f ? female_surname_list
                                           : surname_list;
        return _tables.at(list).at(c).layers();
    }

    /// @brief Hash of the loaded names, recorded in each result's
    ///        signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const { return _data_hash; }

  private:
    static constexpr std::string_view library_name_ = "name-generator";
    static constexpr std::string_view marker_file_ = "american/male.names";
    static constexpr std::string_view conventions_file_ = "conventions.tsv";
    static constexpr std::string_view forms_file_ = "surname_forms.tsv";

    // Indexed by culture.
    static constexpr std::array<std::string_view, 105> culture_codes_{
        "af", "al", "dz", "us", "ao", "ar", "at", "az", "bh", "bd", "be", "bo", "bw", "br", "gb",
        "bn", "bg", "bf", "bi", "kh", "cm", "ca", "cl", "cn", "co", "cr", "hr", "cy", "cz", "dk",
        "dj", "nl", "ec", "eg", "ae", "ee", "et", "fj", "ph", "fi", "fr", "ge", "de", "gh", "gr",
        "gt", "ht", "hn", "hk", "hu", "is", "in", "id", "ir", "iq", "ie", "il", "it", "jm", "jp",
        "jo", "kz", "kr", "kw", "lb", "ly", "lt", "lu", "mo", "my", "mv", "mt", "mu", "mx", "md",
        "ma", "na", "ng", "no", "om", "ps", "pa", "pe", "pl", "pt", "pr", "qa", "ru", "sv", "sa",
        "rs", "sg", "si", "za", "es", "sd", "se", "ch", "sy", "tw", "tn", "tr", "tm", "uy", "ye"};

    static constexpr std::array<std::string_view, 105> culture_names_{
        "afghan",       "albanian",     "algerian",    "american",      "angolan",
        "argentinian",  "austrian",     "azerbaijani", "bahraini",      "bangladeshi",
        "belgian",      "bolivian",     "botswanan",   "brazilian",     "british",
        "bruneian",     "bulgarian",    "burkinabe",   "burundian",     "cambodian",
        "cameroonian",  "canadian",     "chilean",     "chinese",       "colombian",
        "costarican",   "croatian",     "cypriot",     "czech",         "danish",
        "djiboutian",   "dutch",        "ecuadorian",  "egyptian",      "emirati",
        "estonian",     "ethiopian",    "fijian",      "filipino",      "finnish",
        "french",       "georgian",     "german",      "ghanaian",      "greek",
        "guatemalan",   "haitian",      "honduran",    "hongkonger",    "hungarian",
        "icelandic",    "indian",       "indonesian",  "iranian",       "iraqi",
        "irish",        "israeli",      "italian",     "jamaican",      "japanese",
        "jordanian",    "kazakh",       "korean",      "kuwaiti",       "lebanese",
        "libyan",       "lithuanian",   "luxembourgish", "macanese",    "malaysian",
        "maldivian",    "maltese",      "mauritian",   "mexican",       "moldovan",
        "moroccan",     "namibian",     "nigerian",    "norwegian",     "omani",
        "palestinian",  "panamanian",   "peruvian",    "polish",        "portuguese",
        "puertorican",  "qatari",       "russian",     "salvadoran",    "saudi",
        "serbian",      "singaporean",  "slovenian",   "southafrican",  "spanish",
        "sudanese",     "swedish",      "swiss",       "syrian",        "taiwanese",
        "tunisian",     "turkish",      "turkmen",     "uruguayan",     "yemeni"};

    static_assert(culture_codes_.size() == static_cast<std::size_t>(culture::any));
    static_assert(culture_names_.size() == static_cast<std::size_t>(culture::any));

    // The kinds of name list, as indices into _tables. The gendered surname
    // lists exist only for cultures whose surnames change with gender; the
    // others fall back to surname_list.
    using list_kind = std::size_t;
    static constexpr list_kind male_list = 0;
    static constexpr list_kind female_list = 1;
    static constexpr list_kind surname_list = 2;
    static constexpr list_kind male_surname_list = 3;
    static constexpr list_kind female_surname_list = 4;
    static constexpr std::size_t list_count = 5;

    // Names of one list, ready for drawing. The names lie one after the
    // other in a single buffer: a string each would cost 32 bytes and often
    // an allocation on top of the name.
    struct name_pool
    {
        std::wstring text;               // every name, in order
        std::vector<std::uint32_t> ends; // where each name ends in text
        core::discrete dist{1.0};

        [[nodiscard]] bool empty() const noexcept { return ends.empty(); }

        // Every name, as strings (the tests compare against them).
        [[nodiscard]] std::vector<std::wstring> names() const
        {
            std::vector<std::wstring> out;
            out.reserve(ends.size());
            for (std::size_t i = 0; i < ends.size(); ++i) out.push_back(at(i));
            return out;
        }

        [[nodiscard]] bool contains(std::wstring_view name) const
        {
            std::size_t first = 0;
            for (const std::uint32_t end : ends)
            {
                if (std::wstring_view{text}.substr(first, end - first) == name) return true;
                first = end;
            }
            return false;
        }

        // The name at @p i; throws std::out_of_range past the last.
        [[nodiscard]] std::wstring at(std::size_t i) const
        {
            const std::size_t end = ends.at(i);
            const std::size_t first = i == 0 ? 0 : ends[i - 1];
            return text.substr(first, end - first);
        }
    };

    // The hash data_hash() returns: every table's layers, by list and culture.
    [[nodiscard]] static std::uint64_t hash_of_(
        const std::array<std::map<culture, core::layered_table>, list_count>& tables)
    {
        std::uint64_t h = 0;
        for (std::size_t list = 0; list < tables.size(); ++list)
        {
            for (const auto& [c, table] : tables.at(list))
            {
                h = core::hash_combine(h, (list << 8U) | static_cast<std::uint64_t>(c));
                h = core::hash_combine(h, table.data_hash());
            }
        }
        return h;
    }

    // One parsed .names file.
    struct names_file
    {
        culture c;
        list_kind list;
        core::tsv_document doc;
    };

    // What a data directory holds: name lists, and the two convention tables.
    struct data_bundle
    {
        std::vector<names_file> names;
        std::optional<core::tsv_document> conventions;
        std::optional<core::tsv_document> forms;
    };

    // One rule of surname_forms.tsv: a male ending and what replaces it.
    struct form_rule
    {
        std::wstring male;    // empty for `*`: nothing is removed
        std::wstring female;
        std::wstring married;
    };

    // Layered data per list kind and culture, and the pools built from it.
    std::array<std::map<culture, core::layered_table>, list_count> _tables;
    std::uint64_t _data_hash{0}; // hash_of_(_tables), kept with it: every result needs it
    std::map<culture, name_pool> _m_pool;
    std::map<culture, name_pool> _f_pool;
    std::map<culture, name_pool> _sur_pool;
    std::map<culture, name_pool> _m_sur_pool;
    std::map<culture, name_pool> _f_sur_pool;
    core::layered_table _convention_table{convention_schema_()};
    core::layered_table _form_table{form_schema_()};
    std::map<culture, naming_convention> _conventions;
    std::map<culture, std::vector<form_rule>> _forms;
    std::uint64_t _convention_hash{0};

    // Per-instance random engine for seed drawing.
    core::rng _engine{core::rng::from_entropy()};

    // Tag type for the auto-probing singleton constructor.
    struct auto_probe_tag {};

    // Singleton constructor: loads lite, or full when lite is not found.
    explicit ng(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_NAME_EMBED
        return embedded::namegen::files;
#else
        return {};
#endif
    }

    [[nodiscard]] static constexpr list_kind list_of_(gender g)
    {
        switch (g)
        {
        case gender::m: return male_list;
        case gender::f: return female_list;
        case gender::any: return surname_list;
        }
        return surname_list;
    }

    [[nodiscard]] const std::map<culture, name_pool>& pools_(list_kind list) const
    {
        if (list == male_list) return _m_pool;
        if (list == female_list) return _f_pool;
        if (list == male_surname_list) return _m_sur_pool;
        if (list == female_surname_list) return _f_sur_pool;
        return _sur_pool;
    }

    // -- Data ---------------------------------------------------------------

    static core::table_schema schema_()
    {
        return {.key = {"name"}, .required = {}, .optional = {"weight"}};
    }

    // A row's weight: 1 when the cell is empty, otherwise a positive number.
    static double weight_of_(const core::tsv_row& row)
    {
        const auto w = row.get_or<double>("weight", 1.0);
        if (!(w > 0.0)) throw row.error("weight", "must be positive");
        return w;
    }

    // Parse the .names format: the country code and the gender on the first
    // two lines, then `name<TAB>weight` rows. The two header lines are kept
    // as comments, so errors give the file's own line numbers.
    static names_file parse_names_(std::string text, std::string source)
    {
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
        // A header line's text and where the next line starts; the gender
        // line may end the file.
        auto header_line = [&](std::size_t from, std::size_t line) {
            auto end = text.find('\n', from);
            if (end == std::string::npos)
            {
                if (line == 1 || from >= text.size())
                {
                    throw core::tsv_error{source, line, "", "expected a country code and a gender line"};
                }
                end = text.size();
            }
            auto value = std::string_view{text}.substr(from, end - from);
            if (value.ends_with('\r')) value.remove_suffix(1);
            return std::pair{std::string{value}, std::min(end + 1, text.size())};
        };
        const auto [code, after_code] = header_line(0, 1);
        const auto [gender_text, after_gender] = header_line(after_code, 2);
        (void)after_gender;

        const auto c = to_culture(core::from_utf8(code));
        if (c == culture::any)
        {
            throw core::tsv_error{source, 1, "", "unknown country code '" + code + "'"};
        }
        list_kind list = surname_list;
        if (gender_text == "male" || gender_text == "m")
        {
            list = male_list;
        }
        else if (gender_text == "female" || gender_text == "f")
        {
            list = female_list;
        }
        else if (gender_text == "male-surname")
        {
            list = male_surname_list;
        }
        else if (gender_text == "female-surname")
        {
            list = female_surname_list;
        }
        else if (gender_text != "any")
        {
            throw core::tsv_error{source, 2, "",
                                  "expected male, female, any, male-surname or female-surname, got '" +
                                      gender_text + "'"};
        }

        text.insert(after_code, "#");
        text.insert(0, "#");
        return {.c = c, .list = list,
                .doc = core::tsv_document::parse(text, std::move(source), {"name", "weight", "disabled"})};
    }

    static names_file read_file_(const std::filesystem::path& file)
    {
        return parse_names_(read_text_(file), file.string());
    }

    static std::string read_text_(const std::filesystem::path& file)
    {
        std::ifstream in{file, std::ios::binary};
        if (!in) throw core::tsv_error{file.string(), 0, "", "cannot open file"};
        in.seekg(0, std::ios::end);
        const auto size = in.tellg();
        std::string text(size > 0 ? static_cast<std::size_t>(size) : 0, '\0');
        in.seekg(0, std::ios::beg);
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(in.gcount()));
        return text;
    }

    static data_bundle read_directory_(const std::filesystem::path& dir)
    {
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".names")
            {
                paths.push_back(entry.path());
            }
        }
        std::ranges::sort(paths); // the iteration order is unspecified
        data_bundle bundle;
        bundle.names.reserve(paths.size());
        for (const auto& path : paths) bundle.names.push_back(read_file_(path));
        std::error_code ec;
        if (const auto file = dir / conventions_file_; std::filesystem::is_regular_file(file, ec))
        {
            bundle.conventions = core::tsv_document::parse(read_text_(file), file.string());
        }
        if (const auto file = dir / forms_file_; std::filesystem::is_regular_file(file, ec))
        {
            bundle.forms = core::tsv_document::parse(read_text_(file), file.string());
        }
        return bundle;
    }

    // The lookup maps and the hash of the convention tables.
    void rebuild_conventions_()
    {
        _conventions.clear();
        for (const auto& row : _convention_table.rows())
        {
            _conventions.emplace(culture_from_name_(row["culture"]), convention_of_(row));
        }
        _forms.clear();
        for (const auto& row : _form_table.rows())
        {
            _forms[culture_from_name_(row["culture"])].push_back(form_of_(row));
        }
        _convention_hash = core::hash_combine(_convention_table.data_hash(), _form_table.data_hash());
    }

    // Copies of the convention tables with the bundle's layers applied.
    [[nodiscard]] std::pair<core::layered_table, core::layered_table> extended_convention_tables_(
        data_bundle& bundle, core::layer_mode mode, bool reset) const
    {
        auto conventions = reset ? core::layered_table{convention_schema_()} : _convention_table;
        auto forms = reset ? core::layered_table{form_schema_()} : _form_table;
        const auto layer_mode = reset ? core::layer_mode::replace : mode;
        if (bundle.conventions)
        {
            (void)conventions.extend(std::move(*bundle.conventions), layer_mode,
                                     [](const core::tsv_row& row) { (void)convention_of_(row); });
        }
        if (bundle.forms)
        {
            (void)forms.extend(std::move(*bundle.forms), layer_mode,
                               [](const core::tsv_row& row) { (void)form_of_(row); });
        }
        return {std::move(conventions), std::move(forms)};
    }

    // Apply layers to copies of the affected tables, then commit: all or
    // nothing. With @p reset (load), every list is replaced.
    void apply_(data_bundle bundle, core::layer_mode mode, bool reset)
    {
        // The convention tables first, on copies: a bad one changes nothing.
        auto [next_conventions, next_forms] = extended_convention_tables_(bundle, mode, reset);

        auto& files = bundle.names;
        auto next = reset ? decltype(_tables){} : _tables;
        // On reset each list's first file replaces; later files for the same
        // list extend it.
        std::array<std::map<culture, bool>, list_count> started;
        std::array<std::map<culture, bool>, list_count> touched;
        for (auto& file : files)
        {
            auto& tables = next.at(file.list);
            auto it = tables.try_emplace(file.c, schema_()).first;
            auto list_mode = mode;
            if (reset)
            {
                list_mode = started.at(file.list)[file.c] ? core::layer_mode::override
                                                          : core::layer_mode::replace;
            }
            started.at(file.list)[file.c] = true;
            touched.at(file.list)[file.c] = true;
            (void)it->second.extend(std::move(file.doc), list_mode,
                                    [](const core::tsv_row& row) { (void)weight_of_(row); });
        }

        // Rebuild the pools of the lists that changed.
        std::array<std::map<culture, name_pool>, list_count> pools;
        for (std::size_t list = 0; list < pools.size(); ++list)
        {
            if (!reset) pools.at(list) = pools_(list);
            for (const auto& [c, changed] : touched.at(list))
            {
                const auto& table = next.at(list).at(c);
                pools.at(list).erase(c);
                if (table.empty()) continue;
                name_pool pool;
                std::vector<double> weights;
                weights.reserve(table.size());
                pool.ends.reserve(table.size());
                // A name has at most as many characters as bytes.
                std::size_t bytes = 0;
                for (const auto& row : table.rows()) bytes += row["name"].size();
                if (bytes >= std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::length_error("name-generator: too many names in one list");
                }
                pool.text.reserve(bytes);
                for (const auto& row : table.rows())
                {
                    pool.text += core::from_utf8(row["name"]);
                    pool.ends.push_back(static_cast<std::uint32_t>(pool.text.size()));
                    weights.push_back(weight_of_(row));
                }
                if (pool.text.capacity() - pool.text.size() > pool.text.size() / 8)
                {
                    pool.text.shrink_to_fit();
                }
                pool.dist = core::discrete{weights};
                pools.at(list).emplace(c, std::move(pool));
            }
        }

        _convention_table = std::move(next_conventions);
        _form_table = std::move(next_forms);
        rebuild_conventions_();

        _tables = std::move(next);
        _data_hash = hash_of_(_tables);
        _m_pool = std::move(pools.at(male_list));
        _f_pool = std::move(pools.at(female_list));
        _sur_pool = std::move(pools.at(surname_list));
        _m_sur_pool = std::move(pools.at(male_surname_list));
        _f_sur_pool = std::move(pools.at(female_surname_list));
    }

    // -- Generation ---------------------------------------------------------

    // Resolve `any` culture to a random culture from those actually loaded.
    static culture resolve_culture(culture c, const std::map<culture, name_pool>& db,
                                   core::rng& engine)
    {
        if (c != culture::any) return c;
        if (db.empty())
        {
            throw std::invalid_argument("No names loaded for any culture");
        }
        auto it = db.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(core::bounded(engine, db.size())));
        return it->first;
    }

    // Resolve `any` gender to a concrete random gender.
    static gender resolve_gender(gender g, core::rng& engine)
    {
        if (g != gender::any) return g;
        return core::bernoulli(engine, 0.5) ? gender::f : gender::m;
    }

    // Convert a gender enum to its display name for error messages.
    [[nodiscard]] static const char* gender_label(gender g)
    {
        switch (g)
        {
        case gender::m:   return "male";
        case gender::f:   return "female";
        case gender::any: return "any";
        }
        return "unknown";
    }

    // Pick a weighted-random name/surname from the appropriate map.
    [[nodiscard]] static std::wstring pick(const std::map<culture, name_pool>& db,
                                           culture c, gender g, core::rng& engine)
    {
        auto it = db.find(c);
        if (it == db.end() || it->second.empty())
        {
            throw std::invalid_argument(
                std::string("No ") + gender_label(g) + " names loaded for culture '" +
                std::string{culture_name(c)} + "'");
        }
        const auto& pool = it->second;
        return pool.at(pool.dist(engine));
    }

    // One forename: gender, culture and name from their own streams.
    [[nodiscard]] std::tuple<std::wstring, gender, culture> draw_forename_(
        gender g, culture c, const core::rng& r) const
    {
        auto rg = r.split("gender");
        const gender resolved_g = resolve_gender(g, rg);
        const auto& db = resolved_g == gender::f ? _f_pool : _m_pool;
        auto rc = r.split("culture");
        const culture resolved_c = resolve_culture(c, db, rc);
        auto rn = r.split("name");
        return {pick(db, resolved_c, resolved_g, rn), resolved_g, resolved_c};
    }

    // One surname: culture and name from their own streams. A culture with
    // a surname list for the gender draws from it, the others from the list
    // shared by both.
    [[nodiscard]] std::pair<std::wstring, culture> draw_surname_(gender g, culture c,
                                                                 const core::rng& r) const
    {
        auto rc = r.split("culture");
        const culture resolved_c = resolve_culture(c, _sur_pool, rc);
        auto rn = r.split("name");
        const auto& gendered = g == gender::f ? _f_sur_pool : _m_sur_pool;
        const auto& db = gendered.contains(resolved_c) ? gendered : _sur_pool;
        return {pick(db, resolved_c, gender::any, rn), resolved_c};
    }

    // -- Conventions and composing names -------------------------------

    static core::table_schema convention_schema_()
    {
        return {.key = {"culture"},
                .required = {},
                .optional = {"order", "patronymic", "family_name", "surnames", "second_surname",
                             "surname_order"}};
    }

    static core::table_schema form_schema_()
    {
        return {.key = {"culture", "male"}, .required = {"female"}, .optional = {"married"}};
    }

    [[nodiscard]] static culture culture_from_name_(std::string_view name)
    {
        for (std::size_t i = 0; i < culture_names_.size(); ++i)
        {
            if (culture_names_.at(i) == name) return static_cast<culture>(i);
        }
        return culture::any;
    }

    static void check_culture_(const core::tsv_row& row)
    {
        if (culture_from_name_(row["culture"]) == culture::any)
        {
            throw row.error("culture", "unknown culture '" + std::string{row["culture"]} + "'");
        }
    }

    // One row of conventions.tsv, checked.
    static naming_convention convention_of_(const core::tsv_row& row)
    {
        check_culture_(row);
        naming_convention cv;
        const auto order = row.get_or<std::string>("order", "given_family");
        if (order == "family_given")
        {
            cv.order = name_order::family_given;
        }
        else if (order != "given_family")
        {
            throw row.error("order", "expected given_family or family_given, got '" + order + "'");
        }
        const auto patronymic = row.get_or<std::string>("patronymic", "none");
        if (patronymic == "east_slavic")
        {
            cv.patronymic = patronymic_style::east_slavic;
        }
        else if (patronymic == "icelandic")
        {
            cv.patronymic = patronymic_style::icelandic;
        }
        else if (patronymic != "none")
        {
            throw row.error("patronymic",
                            "expected none, east_slavic or icelandic, got '" + patronymic + "'");
        }
        cv.family_name = row.get_or<bool>("family_name", true);
        cv.surnames = row.get_or<int>("surnames", 1);
        if (cv.surnames < 1 || cv.surnames > 2) throw row.error("surnames", "must be 1 or 2");
        cv.second_surname = row.get_or<double>("second_surname", 1.0);
        if (cv.second_surname < 0.0 || cv.second_surname > 1.0)
        {
            throw row.error("second_surname", "must be between 0 and 1");
        }
        const auto inheritance = row.get_or<std::string>("surname_order", "paternal_first");
        if (inheritance == "maternal_first")
        {
            cv.inheritance = surname_order::maternal_first;
        }
        else if (inheritance != "paternal_first")
        {
            throw row.error("surname_order",
                            "expected paternal_first or maternal_first, got '" + inheritance + "'");
        }
        return cv;
    }

    // One row of surname_forms.tsv, checked.
    static form_rule form_of_(const core::tsv_row& row)
    {
        check_culture_(row);
        if (!row.has("male")) throw row.error("male", "missing value (use * for any ending)");
        if (!row.has("female")) throw row.error("female", "missing value");
        form_rule rule;
        rule.male = row["male"] == "*" ? std::wstring{} : core::from_utf8(row["male"]);
        rule.female = core::from_utf8(row["female"]);
        rule.married = row.has("married") ? core::from_utf8(row["married"]) : rule.female;
        return rule;
    }

    [[nodiscard]] std::uint64_t composed_hash_() const
    {
        return core::hash_combine(_data_hash, _convention_hash);
    }

    // A culture with forenames for the genders needed and surnames, drawn
    // from those loaded when @p c is `any`.
    [[nodiscard]] culture resolve_composed_culture_(culture c, bool need_male, bool need_female,
                                                    core::rng& engine) const
    {
        if (c != culture::any) return c;
        std::vector<culture> candidates;
        for (const auto& [culture_id, pool] : _sur_pool)
        {
            if (pool.empty()) continue;
            if (need_male && !_m_pool.contains(culture_id)) continue;
            if (need_female && !_f_pool.contains(culture_id)) continue;
            candidates.push_back(culture_id);
        }
        if (candidates.empty()) throw std::invalid_argument("No names loaded for any culture");
        return candidates.at(core::bounded(engine, candidates.size()));
    }

    [[nodiscard]] std::wstring pick_forename_(gender g, culture c, core::rng r) const
    {
        return pick(g == gender::f ? _f_pool : _m_pool, c, g, r);
    }

    // A surname from the gendered list where the culture has one.
    [[nodiscard]] std::wstring pick_surname_(gender g, culture c, core::rng r) const
    {
        const auto& gendered = g == gender::f ? _f_sur_pool : _m_sur_pool;
        return pick(gendered.contains(c) ? gendered : _sur_pool, c, gender::any, r);
    }

    // The words of one person's name, before they are put in order.
    struct person
    {
        gender g;
        std::wstring given;
        std::wstring patronymic;
        std::vector<std::wstring> family; // one surname, or two
    };

    [[nodiscard]] person draw_person_(gender g, culture c, const core::rng& r) const
    {
        const auto cv = convention(c);
        person p{.g = g, .given = pick_forename_(g, c, r.split("given")), .patronymic = {}, .family = {}};
        if (cv.patronymic != patronymic_style::none)
        {
            const auto father = pick_forename_(gender::m, c, r.split("father"));
            p.patronymic = patronymic_of_(cv.patronymic, father, g == gender::f);
        }
        if (cv.family_name)
        {
            p.family.push_back(pick_surname_(g, c, r.split("family")));
            auto rs = r.split("second");
            if (cv.surnames == 2 && core::bernoulli(rs, cv.second_surname))
            {
                // A second surname that differs from the first when it can.
                const auto rf = r.split("family2");
                std::wstring second;
                for (std::size_t attempt = 0; attempt < 8; ++attempt)
                {
                    second = pick_surname_(g, c, rf.split(attempt));
                    if (second != p.family.front()) break;
                }
                p.family.push_back(std::move(second));
            }
        }
        return p;
    }

    // The words of a name, by role.
    [[nodiscard]] static person person_of_(const name& n)
    {
        person p{.g = n._gender, .given = n.given(), .patronymic = n.patronymic(), .family = {}};
        for (std::size_t i = 0; i < n._parts.size(); ++i)
        {
            if (n._roles.at(i) == name_role::family || n._roles.at(i) == name_role::family2)
            {
                p.family.push_back(n._parts.at(i));
            }
        }
        return p;
    }

    // A person as a name: the parts in the culture's order, with their roles.
    [[nodiscard]] name make_name_(const person& p, culture c, std::uint64_t seed) const
    {
        const auto cv = convention(c);
        using part = std::pair<name_role, const std::wstring*>;
        std::vector<part> given_parts;
        given_parts.emplace_back(name_role::given, &p.given);
        if (!p.patronymic.empty()) given_parts.emplace_back(name_role::patronymic, &p.patronymic);
        std::vector<part> family_parts;
        family_parts.reserve(p.family.size());
        for (std::size_t i = 0; i < p.family.size(); ++i)
        {
            family_parts.emplace_back(i == 0 ? name_role::family : name_role::family2, &p.family.at(i));
        }
        const bool family_first = cv.order == name_order::family_given;
        std::vector<part> sequence = family_first ? family_parts : given_parts;
        const auto& rest = family_first ? given_parts : family_parts;
        sequence.insert(sequence.end(), rest.begin(), rest.end());

        name result{*sequence.front().second, p.g, c, this, sequence.front().first};
        for (std::size_t i = 1; i < sequence.size(); ++i)
        {
            result.append_part(*sequence.at(i).second, sequence.at(i).first);
        }
        result._composed = true;
        result._signature = {.seed = seed, .data = composed_hash_()};
        return result;
    }

    // The family name of a child, from the parents' names.
    [[nodiscard]] std::vector<std::wstring> child_family_(const person& father, const person& mother,
                                                          gender g, culture c,
                                                          const naming_convention& cv) const
    {
        if (!cv.family_name || father.family.empty()) return {};
        if (cv.surnames == 2 && (father.family.size() == 2 || mother.family.size() == 2))
        {
            if (cv.inheritance == surname_order::maternal_first)
            {
                return {mother.family.back(), father.family.back()};
            }
            return {father.family.front(), mother.family.front()};
        }
        return {g == gender::f ? feminize_(father.family.back(), c, false) : father.family.back()};
    }

    // The female form of a male surname: the longest matching ending of the
    // culture's rules, or the surname as it is.
    [[nodiscard]] std::wstring feminize_(const std::wstring& surname, culture c, bool married) const
    {
        const auto it = _forms.find(c);
        if (it == _forms.end()) return surname;
        const form_rule* best = nullptr;
        for (const auto& rule : it->second)
        {
            if (rule.male.size() > surname.size()) continue;
            if (!std::wstring_view{surname}.ends_with(rule.male)) continue;
            if (best == nullptr || rule.male.size() > best->male.size()) best = &rule;
        }
        if (best == nullptr) return surname;
        return surname.substr(0, surname.size() - best->male.size()) +
               (married ? best->married : best->female);
    }

    // The male surname a female form comes from: the longest female ending
    // that undoes to a surname the rules turn back into this one, or the
    // surname as it is.
    [[nodiscard]] std::wstring masculine_(const std::wstring& surname, culture c) const
    {
        const auto it = _forms.find(c);
        if (it == _forms.end()) return surname;
        std::wstring best = surname;
        std::size_t best_size = 0;
        for (const auto& rule : it->second)
        {
            if (rule.female.size() < best_size || rule.female.size() > surname.size()) continue;
            if (!std::wstring_view{surname}.ends_with(rule.female)) continue;
            auto candidate = surname.substr(0, surname.size() - rule.female.size()) + rule.male;
            if (feminize_(candidate, c, false) != surname) continue;
            best = std::move(candidate);
            best_size = rule.female.size();
        }
        return best;
    }

    // A father's given name that gives the child's patronymic: drawn from
    // the male forenames that do, or `fallback` when none does.
    [[nodiscard]] std::wstring father_given_(patronymic_style style, const person& kid, culture c,
                                             const std::wstring& fallback, core::rng r) const
    {
        const auto it = _m_pool.find(c);
        if (it == _m_pool.end()) return fallback;
        std::vector<std::wstring> fits;
        for (std::size_t i = 0; i < it->second.ends.size(); ++i)
        {
            auto candidate = it->second.at(i);
            if (patronymic_of_(style, candidate, kid.g == gender::f) == kid.patronymic)
            {
                fits.push_back(std::move(candidate));
            }
        }
        if (fits.empty()) return fallback;
        return fits.at(core::bounded(r, fits.size()));
    }

    [[nodiscard]] static bool ends_with_(const std::wstring& text, std::wstring_view ending)
    {
        return text.size() >= ending.size() &&
               std::wstring_view{text}.substr(text.size() - ending.size()) == ending;
    }

    [[nodiscard]] static std::wstring patronymic_of_(patronymic_style style, const std::wstring& father,
                                                     bool daughter)
    {
        switch (style)
        {
        case patronymic_style::east_slavic: return east_slavic_patronymic_(father, daughter);
        case patronymic_style::icelandic: return icelandic_patronymic_(father, daughter);
        case patronymic_style::none: break;
        }
        return {};
    }

    // Russian-style patronymics from a transliterated father's name.
    [[nodiscard]] static std::wstring east_slavic_patronymic_(const std::wstring& father, bool daughter)
    {
        struct irregular
        {
            std::wstring_view name;
            std::wstring_view son;
            std::wstring_view daughter;
        };
        static constexpr std::array<irregular, 12> exceptions{{
            {.name = L"Pavel", .son = L"Pavlovich", .daughter = L"Pavlovna"},
            {.name = L"Lev", .son = L"Lvovich", .daughter = L"Lvovna"},
            {.name = L"Pyotr", .son = L"Petrovich", .daughter = L"Petrovna"},
            {.name = L"Petr", .son = L"Petrovich", .daughter = L"Petrovna"},
            {.name = L"Dmitry", .son = L"Dmitriyevich", .daughter = L"Dmitriyevna"},
            {.name = L"Igor", .son = L"Igorevich", .daughter = L"Igorevna"},
            {.name = L"Yakov", .son = L"Yakovlevich", .daughter = L"Yakovlevna"},
            {.name = L"Ilya", .son = L"Ilyich", .daughter = L"Ilyinichna"},
            {.name = L"Nikita", .son = L"Nikitich", .daughter = L"Nikitichna"},
            {.name = L"Luka", .son = L"Lukich", .daughter = L"Lukinichna"},
            {.name = L"Kuzma", .son = L"Kuzmich", .daughter = L"Kuzminichna"},
            {.name = L"Foma", .son = L"Fomich", .daughter = L"Fominichna"},
        }};
        if (father.empty()) return {};
        for (const auto& e : exceptions)
        {
            if (father == e.name) return std::wstring{daughter ? e.daughter : e.son};
        }
        auto stem = father;
        if (ends_with_(father, L"iy") || ends_with_(father, L"ey") || ends_with_(father, L"ay") ||
            ends_with_(father, L"oy") || ends_with_(father, L"uy"))
        {
            return stem + (daughter ? L"evna" : L"evich");
        }
        if (ends_with_(father, L"y"))
        {
            stem.pop_back();
            return stem + (daughter ? L"yevna" : L"yevich");
        }
        if (ends_with_(father, L"a"))
        {
            stem.pop_back();
            return stem + (daughter ? L"ichna" : L"ich");
        }
        for (const wchar_t vowel : std::wstring_view{L"eiou"})
        {
            if (father.back() == vowel) return stem + (daughter ? L"vna" : L"vich");
        }
        return stem + (daughter ? L"ovna" : L"ovich");
    }

    // Icelandic patronymics: the father's name in the genitive, then son or
    // dóttir.
    [[nodiscard]] static std::wstring icelandic_patronymic_(const std::wstring& father, bool daughter)
    {
        auto stem = father;
        if (ends_with_(stem, L"ur"))
        {
            stem.resize(stem.size() - 2);
            stem += L"s";
        }
        else if (ends_with_(stem, L"i"))
        {
            stem.pop_back();
            stem += L"a";
        }
        else if (!ends_with_(stem, L"s"))
        {
            stem += L"s";
        }
        return stem + (daughter ? L"dóttir" : L"son");
    }

    // Core generation logic.
    [[nodiscard]] name solver(bool is_name, gender requested_gender,
                              culture requested_culture, std::uint64_t call_seed) const
    {
        const core::rng r{call_seed};
        name result = [&]() -> name {
            if (is_name)
            {
                auto [text, g, c] = draw_forename_(requested_gender, requested_culture, r);
                return {std::move(text), g, c, this};
            }
            // A surname still has a gender: the one append_name() uses.
            auto rg = r.split("gender");
            const gender g = resolve_gender(requested_gender, rg);
            auto [text, c] = draw_surname_(g, requested_culture, r);
            return {std::move(text), g, c, this, name_role::family};
        }();
        result._signature = {.seed = call_seed, .data = data_hash()};
        return result;
    }

    // Parts appended to a name draw from the name's seed and the part's
    // position, so a seed and the same appends replay the whole name.
    void append_name_impl(name& n, gender g, culture c) const
    {
        const auto r = core::rng{n.seed()}.split("part").split(n._parts.size());
        n.append_part(std::get<0>(draw_forename_(g, c, r)), name_role::given);
    }

    void append_surname_impl(name& n, culture c) const
    {
        const auto r = core::rng{n.seed()}.split("part").split(n._parts.size());
        n.append_part(draw_surname_(n._gender, c, r).first, name_role::family);
    }

    friend class name;
    friend struct ::ng_test_access;
};

// Out-of-line definitions for name methods that call into ng.
inline name& name::append_name()
{
    _owner->append_name_impl(*this, _gender, _culture);
    return *this;
}

inline name& name::append_name(culture c)
{
    _owner->append_name_impl(*this, _gender, c);
    return *this;
}

inline name& name::append_surname()
{
    _owner->append_surname_impl(*this, _culture);
    return *this;
}

inline name& name::append_surname(culture c)
{
    _owner->append_surname_impl(*this, c);
    return *this;
}

inline void name::to_json(core::json_writer& w) const
{
    w.begin_object().field("name", core::to_utf8(_full_string)).key("parts").begin_array();
    for (const auto& part : _parts) w.value(core::to_utf8(part));
    w.end_array();
    if (_composed)
    {
        static constexpr std::array<std::string_view, 4> role_names{"given", "patronymic", "family",
                                                                    "family2"};
        w.key("roles").begin_array();
        for (const auto role : _roles) w.value(role_names.at(static_cast<std::size_t>(role)));
        w.end_array();
    }
    w.field("gender", _gender == gender::f ? "female" : "male")
        .field("culture", ng::culture_name(_culture))
        .key("signature");
    _signature.to_json(w);
    w.end_object();
}

} // namespace dasmig

#endif // DASMIG_NAMEGEN_HPP
