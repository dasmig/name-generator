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

DASMIG_CORE_REQUIRE(1, 0);

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
    name(std::wstring name_str, gender g, culture c, const ng* owner)
        : _full_string(std::move(name_str)), _gender(g), _culture(c), _owner(owner)
    {
        _parts.push_back(_full_string);
    }

    void append_part(std::wstring part)
    {
        _full_string.append(L" ").append(part);
        _parts.push_back(std::move(part));
    }

    std::wstring _full_string;        ///< Full composed name.
    std::vector<std::wstring> _parts; ///< Individual name parts.
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
        std::vector<names_file> files;
        for (const auto& file : embedded_files_())
        {
            if (file.path.starts_with(tier_dir + "/") && file.path.ends_with(".names"))
            {
                files.push_back(parse_names_(file.text(), "embedded:" + std::string{file.path}));
            }
        }
        if (files.empty())
        {
            const auto root = core::find_resources(library_name_, tier_dir + "/" + std::string{marker_file_});
            if (!root) return false;
            files = read_directory_(*root / tier_dir);
        }
        if (files.empty()) return false;
        apply_(std::move(files), core::layer_mode::replace, true);
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
        if (path.extension() != ".names")
        {
            throw std::invalid_argument("extend: expected a .names file, got " +
                                        path.filename().string());
        }
        std::vector<names_file> files;
        files.push_back(read_file_(path));
        apply_(std::move(files), mode, false);
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
        std::vector<names_file> files;
        files.push_back(parse_names_(std::string{text}, std::move(source)));
        const auto list = files.front().list;
        const auto c = files.front().c;
        apply_(std::move(files), mode, false);
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
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        for (std::size_t list = 0; list < _tables.size(); ++list)
        {
            for (const auto& [c, table] : _tables.at(list))
            {
                h = core::hash_combine(h, (list << 8U) | static_cast<std::uint64_t>(c));
                h = core::hash_combine(h, table.data_hash());
            }
        }
        return h;
    }

  private:
    static constexpr std::string_view library_name_ = "name-generator";
    static constexpr std::string_view marker_file_ = "american/male.names";

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

    // Names of one list, ready for drawing.
    struct name_pool
    {
        std::vector<std::wstring> names;
        std::vector<double> weights;
        core::discrete dist{1.0};
    };

    // One parsed .names file.
    struct names_file
    {
        culture c;
        list_kind list;
        core::tsv_document doc;
    };

    // Layered data per list kind and culture, and the pools built from it.
    std::array<std::map<culture, core::layered_table>, list_count> _tables;
    std::map<culture, name_pool> _m_pool;
    std::map<culture, name_pool> _f_pool;
    std::map<culture, name_pool> _sur_pool;
    std::map<culture, name_pool> _m_sur_pool;
    std::map<culture, name_pool> _f_sur_pool;

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
        std::ifstream in{file, std::ios::binary};
        if (!in) throw core::tsv_error{file.string(), 0, "", "cannot open file"};
        std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        return parse_names_(std::move(text), file.string());
    }

    static std::vector<names_file> read_directory_(const std::filesystem::path& dir)
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
        std::vector<names_file> files;
        files.reserve(paths.size());
        for (const auto& path : paths) files.push_back(read_file_(path));
        return files;
    }

    // Apply layers to copies of the affected tables, then commit: all or
    // nothing. With @p reset (load), every list is replaced.
    void apply_(std::vector<names_file> files, core::layer_mode mode, bool reset)
    {
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
                pool.names.reserve(table.size());
                pool.weights.reserve(table.size());
                for (const auto& row : table.rows())
                {
                    pool.names.push_back(core::from_utf8(row["name"]));
                    pool.weights.push_back(weight_of_(row));
                }
                pool.dist = core::discrete{pool.weights};
                pools.at(list).emplace(c, std::move(pool));
            }
        }

        _tables = std::move(next);
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
        if (it == db.end() || it->second.names.empty())
        {
            throw std::invalid_argument(
                std::string("No ") + gender_label(g) + " names loaded for culture '" +
                std::string{culture_name(c)} + "'");
        }
        const auto& pool = it->second;
        return pool.names.at(pool.dist(engine));
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
            return {std::move(text), g, c, this};
        }();
        result._signature = {.seed = call_seed, .data = data_hash()};
        return result;
    }

    // Parts appended to a name draw from the name's seed and the part's
    // position, so a seed and the same appends replay the whole name.
    void append_name_impl(name& n, gender g, culture c) const
    {
        const auto r = core::rng{n.seed()}.split("part").split(n._parts.size());
        n.append_part(std::get<0>(draw_forename_(g, c, r)));
    }

    void append_surname_impl(name& n, culture c) const
    {
        const auto r = core::rng{n.seed()}.split("part").split(n._parts.size());
        n.append_part(draw_surname_(n._gender, c, r).first);
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
    w.end_array()
        .field("gender", _gender == gender::f ? "female" : "male")
        .field("culture", ng::culture_name(_culture))
        .key("signature");
    _signature.to_json(w);
    w.end_object();
}

} // namespace dasmig

#endif // DASMIG_NAMEGEN_HPP
