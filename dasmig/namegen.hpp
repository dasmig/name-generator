#ifndef DASMIG_NAMEGEN_HPP
#define DASMIG_NAMEGEN_HPP

#include "random.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file namegen.hpp
/// @brief Name generator library — culture-aware name generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

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

/// @brief Dataset size tier for resource loading.
#ifndef DASMIG_DATASET_DEFINED
#define DASMIG_DATASET_DEFINED
enum class dataset : std::uint8_t
{
    lite,     ///< Top-500 names per category (~2 MB).
    full      ///< Complete dataset (~39 MB).
};
#endif

/// @brief Simple gender enum to distinguish between male and female names.
enum class gender : std::uint8_t
{
    m,
    f,
    any
};

/// @brief Return type for name generation, holding both individual parts and
/// the full composed string.
///
/// Supports implicit conversion to std::wstring, streaming via operator<<,
/// and chained appending of names and surnames.
class name
{
  public:
    /// @brief Retrieve the random seed used to generate this name.
    /// @return The per-call seed for replay.
    /// @see ng::get_name(gender, culture, std::uint64_t)
    [[nodiscard]] std::uint64_t seed() const
    {
        return _seed;
    }

    /// @brief Return the individual parts (names/surnames) as a vector.
    /// @return Vector of name parts.
    [[nodiscard]] const std::vector<std::wstring>& parts() const
    {
        return _parts;
    }

    /// @brief Append a forename to this name, preserving gender and culture.
    /// @return `*this` for chaining.
    name& append_name();

    /// @brief Append a forename of a specific culture.
    /// @param c Culture for the appended name.
    /// @return `*this` for chaining.
    name& append_name(culture c);

    /// @brief Append a surname to this name, preserving culture.
    /// @return `*this` for chaining.
    name& append_surname();

    /// @brief Append a surname of a specific culture.
    /// @param c Culture for the appended surname.
    /// @return `*this` for chaining.
    name& append_surname(culture c);

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
    name(std::wstring name_str, gender g, culture c, class ng* owner,
         std::uint64_t seed = 0)
        : _full_string(std::move(name_str)), _gender(g), _culture(c),
          _owner(owner), _seed(seed)
    {
        _parts.push_back(_full_string);
    }

    std::wstring _full_string;            ///< Full composed name.
    std::vector<std::wstring> _parts;     ///< Individual name parts.
    gender _gender;                       ///< Gender of the first name.
    culture _culture;                     ///< Culture of the first name.
    class ng* _owner;                     ///< Generator that created this name.
    std::uint64_t _seed{0};               ///< Random seed for replay.

    friend class ng;
};

/// @brief Name generator that produces culture-aware names and surnames.
///
/// Generates realistic names by picking from popular name databases indexed
/// by culture and gender. Supports 105 cultures.
///
/// Can be used as a singleton via instance() or constructed independently.
/// Independent instances own their own name databases and random engine.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_name() on
/// the **same** instance require external synchronization. load() mutates
/// internal state and must not be called concurrently with get_name()
/// on the same instance.
class ng
{
  public:
    /// @brief Default constructor — creates an empty generator with no names.
    ///
    /// Call load() to populate name databases before generating.
    ng() = default;

    ng(const ng&) = delete;            ///< Not copyable.
    ng& operator=(const ng&) = delete; ///< Not copyable.
    ng(ng&&) noexcept = default;                ///< Move constructor.
    ng& operator=(ng&&) noexcept = default;     ///< Move assignment.
    ~ng() = default;                   ///< Default destructor.

    /// @brief Access the global singleton instance.
    ///
    /// The singleton auto-probes common resource paths on first access.
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
        static const std::map<std::wstring, culture, std::less<>>
            country_code_map = {
            {L"ae", culture::emirati},       {L"af", culture::afghan},
            {L"al", culture::albanian},      {L"ao", culture::angolan},
            {L"ar", culture::argentinian},   {L"at", culture::austrian},
            {L"az", culture::azerbaijani},   {L"bd", culture::bangladeshi},
            {L"be", culture::belgian},       {L"bf", culture::burkinabe},
            {L"bg", culture::bulgarian},     {L"bh", culture::bahraini},
            {L"bi", culture::burundian},     {L"bn", culture::bruneian},
            {L"bo", culture::bolivian},      {L"br", culture::brazilian},
            {L"bw", culture::botswanan},     {L"ca", culture::canadian},
            {L"ch", culture::swiss},         {L"cl", culture::chilean},
            {L"cm", culture::cameroonian},   {L"cn", culture::chinese},
            {L"co", culture::colombian},     {L"cr", culture::costarican},
            {L"cy", culture::cypriot},       {L"cz", culture::czech},
            {L"de", culture::german},        {L"dj", culture::djiboutian},
            {L"dk", culture::danish},        {L"dz", culture::algerian},
            {L"ec", culture::ecuadorian},    {L"ee", culture::estonian},
            {L"eg", culture::egyptian},      {L"es", culture::spanish},
            {L"et", culture::ethiopian},     {L"fi", culture::finnish},
            {L"fj", culture::fijian},        {L"fr", culture::french},
            {L"gb", culture::british},       {L"ge", culture::georgian},
            {L"gh", culture::ghanaian},      {L"gr", culture::greek},
            {L"gt", culture::guatemalan},    {L"hk", culture::hongkonger},
            {L"hn", culture::honduran},      {L"hr", culture::croatian},
            {L"ht", culture::haitian},       {L"hu", culture::hungarian},
            {L"id", culture::indonesian},    {L"ie", culture::irish},
            {L"il", culture::israeli},       {L"in", culture::indian},
            {L"iq", culture::iraqi},         {L"ir", culture::iranian},
            {L"is", culture::icelandic},     {L"it", culture::italian},
            {L"jm", culture::jamaican},      {L"jo", culture::jordanian},
            {L"jp", culture::japanese},      {L"kh", culture::cambodian},
            {L"kr", culture::korean},        {L"kw", culture::kuwaiti},
            {L"kz", culture::kazakh},        {L"lb", culture::lebanese},
            {L"lt", culture::lithuanian},    {L"lu", culture::luxembourgish},
            {L"ly", culture::libyan},        {L"ma", culture::moroccan},
            {L"md", culture::moldovan},      {L"mo", culture::macanese},
            {L"mt", culture::maltese},       {L"mu", culture::mauritian},
            {L"mv", culture::maldivian},     {L"mx", culture::mexican},
            {L"my", culture::malaysian},     {L"na", culture::namibian},
            {L"ng", culture::nigerian},      {L"nl", culture::dutch},
            {L"no", culture::norwegian},     {L"om", culture::omani},
            {L"pa", culture::panamanian},    {L"pe", culture::peruvian},
            {L"ph", culture::filipino},      {L"pl", culture::polish},
            {L"pr", culture::puertorican},   {L"ps", culture::palestinian},
            {L"pt", culture::portuguese},    {L"qa", culture::qatari},
            {L"rs", culture::serbian},       {L"ru", culture::russian},
            {L"sa", culture::saudi},         {L"sd", culture::sudanese},
            {L"se", culture::swedish},       {L"sg", culture::singaporean},
            {L"si", culture::slovenian},     {L"sv", culture::salvadoran},
            {L"sy", culture::syrian},        {L"tm", culture::turkmen},
            {L"tn", culture::tunisian},      {L"tr", culture::turkish},
            {L"tw", culture::taiwanese},     {L"us", culture::american},
            {L"uy", culture::uruguayan},     {L"ye", culture::yemeni},
            {L"za", culture::southafrican}};

        if (auto it = country_code_map.find(country_code);
            it != country_code_map.end())
        {
            return it->second;
        }
        return culture::any;
    }

    /// @brief Translate a gender string to a gender enum.
    /// @param gender_string Gender string (e.g., L"male", L"female", L"m", L"f").
    /// @return Matching gender, or gender::any if not recognized.
    [[nodiscard]] static gender to_gender(std::wstring_view gender_string)
    {
        static const std::map<std::wstring, gender, std::less<>> gender_map = {
            {L"m", gender::m},
            {L"f", gender::f},
            {L"male", gender::m},
            {L"female", gender::f}};

        if (auto it = gender_map.find(gender_string);
            it != gender_map.end())
        {
            return it->second;
        }
        return gender::any;
    }

    /// @brief Generate a first name.
    /// @param g Gender (default: random).
    /// @param c Culture (default: random).
    /// @return A name object supporting chained appending.
    /// @throws std::invalid_argument If no names loaded for the resolved culture/gender.
    [[nodiscard]] name get_name(gender g = gender::any,
                                culture c = culture::any)
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        auto result = solver(true, g, c, call_seed);
        result._seed = call_seed;
        return result;
    }

    /// @brief Generate a deterministic first name using a specific seed.
    /// @param g Gender (default: random).
    /// @param c Culture (default: random).
    /// @param call_seed Seed for reproducible results.
    /// @return A name object.
    /// @throws std::invalid_argument If no names loaded for the resolved culture/gender.
    [[nodiscard]] name get_name(gender g, culture c,
                                std::uint64_t call_seed)
    {
        auto result = solver(true, g, c, call_seed);
        result._seed = call_seed;
        return result;
    }

    /// @brief Generate a surname.
    /// @param c Culture (default: random).
    /// @return A name object supporting chained appending.
    /// @throws std::invalid_argument If no surnames loaded for the resolved culture.
    [[nodiscard]] name get_surname(culture c = culture::any)
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        auto result = solver(false, gender::any, c, call_seed);
        result._seed = call_seed;
        return result;
    }

    /// @brief Generate a deterministic surname using a specific seed.
    /// @param c Culture (default: random).
    /// @param call_seed Seed for reproducible results.
    /// @return A name object.
    /// @throws std::invalid_argument If no surnames loaded for the resolved culture.
    [[nodiscard]] name get_surname(culture c,
                                   std::uint64_t call_seed)
    {
        auto result = solver(false, gender::any, c, call_seed);
        result._seed = call_seed;
        return result;
    }

    /// @name Seeding
    /// @{

    /// @brief Seed the internal random engine for deterministic sequences.
    ///
    /// Subsequent get_name() / get_surname() calls (without an explicit seed)
    /// draw per-call seeds from this engine, producing a reproducible sequence.
    ///
    /// @param seed_value The seed value.
    /// @return `*this` for chaining.
    ng& seed(std::uint64_t seed_value)
    {
        _engine.seed(seed_value);
        return *this;
    }

    /// @brief Reseed the engine with a non-deterministic source.
    /// @return `*this` for chaining.
    ng& unseed()
    {
        _engine.seed(std::random_device{}());
        return *this;
    }

    /// @}

    /// @brief Check whether any name databases have been loaded.
    /// @return `true` if at least one name file has been loaded.
    [[nodiscard]] bool has_resources() const
    {
        return !_m_pool.empty() ||
               !_f_pool.empty() ||
               !_sur_pool.empty();
    }

    /// @brief Load name files from a directory.
    ///
    /// Recursively scans @p resource_path for `.names` files and indexes them
    /// by culture and gender. Safe to call multiple times.
    ///
    /// @param resource_path Directory containing `.names` files.
    void load(const std::filesystem::path& resource_path)
    {
        if (std::filesystem::exists(resource_path) &&
            std::filesystem::is_directory(resource_path))
        {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(resource_path))
            {
                if (entry.is_regular_file() &&
                    (entry.path().extension() == ".names"))
                {
                    parse_file(entry);
                }
            }
        }
    }

    /// @brief Load a specific dataset tier from a base resources directory.
    ///
    /// Probes common base paths ("resources", "../resources",
    /// "name-generator/resources") and loads from the `lite/` or `full/`
    /// subfolder according to @p tier.
    ///
    /// @param tier The dataset size to load (dataset::lite or dataset::full).
    /// @return `true` if a matching directory was found and loaded.
    bool load(dataset tier)
    {
        static constexpr std::array probe_paths = {
            "resources", "../resources", "name-generator/resources"};

        const char* subfolder =
            (tier == dataset::full) ? "full" : "lite";

        auto found = std::ranges::find_if(probe_paths, [&](const char* base) {
            const std::filesystem::path dir =
                std::filesystem::path{base} / subfolder;
            return std::filesystem::is_directory(dir);
        });
        if (found != probe_paths.end())
        {
            load(std::filesystem::path{*found} / subfolder);
            return true;
        }
        return false;
    }

  private:
    // Container of names paired with their selection weights.
    struct name_pool
    {
        std::vector<std::wstring> names;
        std::vector<double> weights;
        // Mutable because discrete_distribution::operator() is non-const
        // (it may update internal generator state), but the distribution
        // parameters are immutable after construction.
        mutable std::discrete_distribution<std::size_t> dist;
    };

    // Maps for accessing names through culture.
    std::map<culture, name_pool> _m_pool;
    std::map<culture, name_pool> _f_pool;
    std::map<culture, name_pool> _sur_pool;

    // Per-instance random engine for seed drawing.
    std::mt19937_64 _engine{std::random_device{}()};

    // Tag type for the auto-probing singleton constructor.
    struct auto_probe_tag {};

    // Singleton constructor: auto-probes common resource locations.
    explicit ng(auto_probe_tag /*tag*/)
    {
        static constexpr std::array probe_paths = {
            "resources", "../resources", "name-generator/resources"};

        auto found = std::ranges::find_if(probe_paths, [](const char* p) {
            return std::filesystem::exists(p) &&
                   std::filesystem::is_directory(p);
        });
        if (found != probe_paths.end())
        {
            const std::filesystem::path base{*found};
            auto lite_dir = base / "lite";
            auto full_dir = base / "full";
            if (std::filesystem::is_directory(lite_dir))
            {
                load(lite_dir);
            }
            else if (std::filesystem::is_directory(full_dir))
            {
                load(full_dir);
            }
            else
            {
                load(base);
            }
        }
    }

    // Resolve `any` culture to a random culture from those actually loaded.
    static culture resolve_culture(culture c,
                                   const std::map<culture, name_pool>& db,
                                   effolkronium::random_local& engine)
    {
        if (c != culture::any) { return c; }
        if (db.empty())
        {
            throw std::invalid_argument("No names loaded for any culture");
        }
        auto idx = engine.get<std::size_t>(0, db.size() - 1);
        auto it = db.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(idx));
        return it->first;
    }

    // Resolve `any` gender to a concrete random gender.
    static gender resolve_gender(gender g,
                                 effolkronium::random_local& engine)
    {
        if (g == gender::any)
        {
            return static_cast<gender>(engine.get<std::size_t>(0, 1));
        }
        return g;
    }

    // Convert a culture enum to its display name for error messages.
    [[nodiscard]] static const char* culture_label(culture c)
    {
        static constexpr std::array labels = {
            "afghan",       "albanian",     "algerian",
            "american",     "angolan",      "argentinian",
            "austrian",     "azerbaijani",  "bahraini",
            "bangladeshi",  "belgian",      "bolivian",
            "botswanan",    "brazilian",    "british",
            "bruneian",     "bulgarian",    "burkinabe",
            "burundian",    "cambodian",    "cameroonian",
            "canadian",     "chilean",      "chinese",
            "colombian",    "costarican",   "croatian",
            "cypriot",      "czech",        "danish",
            "djiboutian",   "dutch",        "ecuadorian",
            "egyptian",     "emirati",      "estonian",
            "ethiopian",    "fijian",       "filipino",
            "finnish",      "french",       "georgian",
            "german",       "ghanaian",     "greek",
            "guatemalan",   "haitian",      "honduran",
            "hongkonger",   "hungarian",    "icelandic",
            "indian",       "indonesian",   "iranian",
            "iraqi",        "irish",        "israeli",
            "italian",      "jamaican",     "japanese",
            "jordanian",    "kazakh",       "korean",
            "kuwaiti",      "lebanese",     "libyan",
            "lithuanian",   "luxembourgish","macanese",
            "malaysian",    "maldivian",    "maltese",
            "mauritian",    "mexican",      "moldovan",
            "moroccan",     "namibian",     "nigerian",
            "norwegian",    "omani",        "palestinian",
            "panamanian",   "peruvian",     "polish",
            "portuguese",   "puertorican",  "qatari",
            "russian",      "salvadoran",   "saudi",
            "serbian",      "singaporean",  "slovenian",
            "southafrican", "spanish",      "sudanese",
            "swedish",      "swiss",        "syrian",
            "taiwanese",    "tunisian",     "turkish",
            "turkmen",      "uruguayan",    "yemeni",
            "any"};
        auto idx = static_cast<std::size_t>(c);
        if (idx < labels.size()) { return labels.at(idx); }
        return "unknown";
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
    [[nodiscard]] static std::wstring pick(
        const std::map<culture, name_pool>& db,
        culture c, gender g, effolkronium::random_local& engine)
    {
        auto it = db.find(c);
        if (it == db.end() || it->second.names.empty())
        {
            throw std::invalid_argument(
                std::string("No ") + gender_label(g) +
                " names loaded for culture '" + culture_label(c) + "'");
        }
        const auto& pool = it->second;
        if (!pool.weights.empty())
        {
            // Use the pre-built distribution — avoids reconstructing
            // the prefix-sum table on every call.
            return pool.names.at(pool.dist(engine.engine()));
        }
        return *engine.get(pool.names);
    }

    /// @brief Number of bits to shift when XOR-folding a 64-bit seed to 32.
    static constexpr unsigned seed_fold_shift = 32U;

    // Append a forename part to an existing name.
    void append_name_impl(name& n, gender g, culture c)
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        effolkronium::random_local call_engine;
        call_engine.seed(static_cast<std::mt19937::result_type>(
            (call_seed ^ (call_seed >> seed_fold_shift))));

        const gender resolved_g = resolve_gender(g, call_engine);

        const auto& db = (resolved_g == gender::f)
                             ? _f_pool : _m_pool;

        const culture resolved_c = resolve_culture(c, db, call_engine);

        const std::wstring part = pick(db, resolved_c, resolved_g,
                                       call_engine);
        n._parts.push_back(part);
        n._full_string.append(L" ").append(part);
    }

    // Append a surname part to an existing name.
    void append_surname_impl(name& n, culture c)
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        effolkronium::random_local call_engine;
        call_engine.seed(static_cast<std::mt19937::result_type>(
            (call_seed ^ (call_seed >> seed_fold_shift))));

        const culture resolved_c = resolve_culture(c,
            _sur_pool, call_engine);
        const std::wstring part = pick(_sur_pool,
                                       resolved_c, gender::any, call_engine);
        n._parts.push_back(part);
        n._full_string.append(L" ").append(part);
    }

    // Core generation logic.
    [[nodiscard]] name solver(bool is_name, gender requested_gender,
                              culture requested_culture,
                              std::uint64_t call_seed)
    {
        effolkronium::random_local call_engine;
        call_engine.seed(static_cast<std::mt19937::result_type>(
            (call_seed ^ (call_seed >> seed_fold_shift))));

        const gender resolved_gender = resolve_gender(requested_gender,
                                                      call_engine);

        if (is_name)
        {
            const auto& db = (resolved_gender == gender::f)
                                 ? _f_pool : _m_pool;
            const culture resolved_culture = resolve_culture(
                requested_culture, db, call_engine);
            return {pick(db, resolved_culture, resolved_gender,
                         call_engine),
                    resolved_gender, resolved_culture, this};
        }

        const culture resolved_culture = resolve_culture(
            requested_culture, _sur_pool, call_engine);
        return {pick(_sur_pool,
                     resolved_culture, gender::any, call_engine),
                resolved_gender, resolved_culture, this};
    }

    /// @brief Decode a UTF-8 byte string into a wide string.
    ///
    /// Handles 1–4 byte sequences and produces UTF-32 on Linux
    /// (wchar_t is 4 bytes) or UTF-16 surrogate pairs on Windows
    /// (wchar_t is 2 bytes). Invalid lead bytes are silently skipped.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    static std::wstring utf8_to_wstring(const std::string& utf8)
    {
        // UTF-8 prefix masks and value masks for each sequence length.
        static constexpr unsigned char ascii_max       = 0x80U;
        static constexpr unsigned char two_byte_mask   = 0xE0U;
        static constexpr unsigned char two_byte_lead   = 0xC0U;
        static constexpr unsigned char two_byte_val    = 0x1FU;
        static constexpr unsigned char three_byte_mask = 0xF0U;
        static constexpr unsigned char three_byte_lead = 0xE0U;
        static constexpr unsigned char three_byte_val  = 0x0FU;
        static constexpr unsigned char four_byte_mask  = 0xF8U;
        static constexpr unsigned char four_byte_lead  = 0xF0U;
        static constexpr unsigned char four_byte_val   = 0x07U;
        static constexpr unsigned char cont_val        = 0x3FU;
        static constexpr unsigned char cont_check_mask  = 0xC0U;
        static constexpr unsigned char cont_check_lead  = 0x80U;
        static constexpr unsigned cont_shift           = 6U;
        static constexpr char32_t max_codepoint        = 0x10FFFFU;
        // Surrogate-pair constants (UTF-16, wchar_t == 2 bytes only).
        static constexpr char32_t surrogate_offset     = 0x10000U;
        static constexpr char32_t high_surrogate_base  = 0xD800U;
        static constexpr char32_t low_surrogate_base   = 0xDC00U;
        static constexpr unsigned surrogate_shift      = 10U;
        static constexpr char32_t surrogate_mask       = 0x3FFU;

        std::wstring result;
        result.reserve(utf8.size());
        std::size_t i = 0;

        while (i < utf8.size())
        {
            char32_t codepoint = 0;
            auto lead = static_cast<unsigned char>(utf8.at(i));
            std::size_t extra = 0;

            if (lead < ascii_max)
            {
                codepoint = lead;
            }
            else if ((lead & two_byte_mask) == two_byte_lead)
            {
                codepoint = lead & two_byte_val;
                extra = 1;
            }
            else if ((lead & three_byte_mask) == three_byte_lead)
            {
                codepoint = lead & three_byte_val;
                extra = 2;
            }
            else if ((lead & four_byte_mask) == four_byte_lead)
            {
                codepoint = lead & four_byte_val;
                extra = 3;
            }
            else
            {
                ++i;
                continue; // skip invalid lead byte
            }

            ++i;
            bool valid = true;
            for (std::size_t j = 0; j < extra; ++j, ++i)
            {
                if (i >= utf8.size())
                {
                    valid = false;
                    break; // truncated sequence
                }
                auto byte = static_cast<unsigned char>(utf8.at(i));
                if ((byte & cont_check_mask) != cont_check_lead)
                {
                    valid = false;
                    break; // not a valid continuation byte
                }
                codepoint = (codepoint << cont_shift) |
                            (static_cast<char32_t>(byte) &
                             static_cast<char32_t>(cont_val));
            }

            if (!valid || codepoint > max_codepoint)
            {
                continue; // skip malformed or out-of-range sequence
            }

            if constexpr (sizeof(wchar_t) >= 4)
            {
                result.push_back(static_cast<wchar_t>(codepoint));
            }
            else
            {
                if (codepoint < surrogate_offset)
                {
                    result.push_back(static_cast<wchar_t>(codepoint));
                }
                else
                {
                    const char32_t shifted = codepoint - surrogate_offset;
                    result.push_back(static_cast<wchar_t>(
                        high_surrogate_base + (shifted >> surrogate_shift)));
                    result.push_back(static_cast<wchar_t>(
                        low_surrogate_base + (shifted & surrogate_mask)));
                }
            }
        }
        return result;
    }

    // Parse a .names file and index it into the appropriate map.
    // Uses std::ifstream (byte-oriented) + utf8_to_wstring so that
    // non-ASCII names load correctly regardless of the system locale.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void parse_file(const std::filesystem::path& file)
    {
        std::ifstream tentative_file{file};

        if (tentative_file.is_open())
        {
            std::string raw_line;

            // Read culture from the first line.
            if (!std::getline(tentative_file, raw_line))
            {
                return;
            }
            if (!raw_line.empty() && raw_line.back() == '\r')
            {
                raw_line.pop_back();
            }
            const culture culture_read =
                to_culture(utf8_to_wstring(raw_line));

            // We can't continue without a valid culture.
            if (culture_read == culture::any)
            {
                return;
            }

            // Read gender from the second line.
            if (!std::getline(tentative_file, raw_line))
            {
                return;
            }
            if (!raw_line.empty() && raw_line.back() == '\r')
            {
                raw_line.pop_back();
            }
            const gender gender_read = to_gender(utf8_to_wstring(raw_line));

            // Read all remaining lines as names with optional weights.
            // Format: "name\tweight" or just "name" (weight defaults to 1.0).
            name_pool pool;
            while (std::getline(tentative_file, raw_line))
            {
                if (!raw_line.empty() && raw_line.back() == '\r')
                {
                    raw_line.pop_back();
                }
                if (!raw_line.empty())
                {
                    auto tab_pos = raw_line.find('\t');
                    if (tab_pos != std::string::npos)
                    {
                        pool.names.push_back(
                            utf8_to_wstring(raw_line.substr(0, tab_pos)));
                        pool.weights.push_back(
                            std::stod(raw_line.substr(tab_pos + 1)));
                    }
                    else
                    {
                        pool.names.push_back(utf8_to_wstring(raw_line));
                        pool.weights.push_back(1.0);
                    }
                }
            }

            if (pool.names.empty())
            {
                return;
            }

            // Pre-build the weighted distribution for O(1) generation.
            if (!pool.weights.empty())
            {
                pool.dist = std::discrete_distribution<std::size_t>(
                    pool.weights.begin(), pool.weights.end());
            }

            // Index by gender.
            switch (gender_read)
            {
            case gender::m:
                _m_pool[culture_read] = std::move(pool);
                break;
            case gender::f:
                _f_pool[culture_read] = std::move(pool);
                break;
            default:
                _sur_pool[culture_read] = std::move(pool);
                break;
            }
        }
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

} // namespace dasmig

#endif // DASMIG_NAMEGEN_HPP
