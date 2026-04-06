#include "catch_amalgamated.hpp"
#include "../dasmig/namegen.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test access helper — exposes private methods and state for testing
// ---------------------------------------------------------------------------
struct ng_test_access
{
    static auto& m_names(dasmig::ng& g)
    {
        return g._culture_indexed_m_names;
    }
    static auto& f_names(dasmig::ng& g)
    {
        return g._culture_indexed_f_names;
    }
    static auto& surnames(dasmig::ng& g)
    {
        return g._culture_indexed_surnames;
    }
    static std::wstring utf8_to_wstring(const std::string& s)
    {
        return dasmig::ng::utf8_to_wstring(s);
    }
};

// ---------------------------------------------------------------------------
// Helper: locate the resources directory (works from repo root or build/)
// ---------------------------------------------------------------------------
static std::filesystem::path resource_path()
{
    for (const auto& candidate : {"resources", "../resources"})
    {
        std::filesystem::path p{candidate};
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p))
        {
            return p;
        }
    }
    return "resources";
}

// ---------------------------------------------------------------------------
// Helper: access the singleton with resources pre-loaded
// ---------------------------------------------------------------------------
static dasmig::ng& gen()
{
    static bool loaded = false;
    if (!loaded)
    {
        dasmig::ng::instance().load(resource_path());
        loaded = true;
    }
    return dasmig::ng::instance();
}

// ===========================================================================
// name class
// ===========================================================================
TEST_CASE("name - implicit conversion to wstring", "[name]")
{
    auto& g = gen();
    g.seed(1);
    auto n = g.get_name(dasmig::gender::m, dasmig::culture::american);
    std::wstring s = n;
    REQUIRE_FALSE(s.empty());
    // Verify that the string matches what parts() reports.
    REQUIRE(s == n.parts().front());
    g.unseed();
}

TEST_CASE("name - parts() returns at least one entry", "[name]")
{
    auto n = gen().get_name();
    REQUIRE(n.parts().size() >= 1);
    REQUIRE(n.parts().front() == static_cast<std::wstring>(n));
}

TEST_CASE("name - seed() returns a non-zero value", "[name]")
{
    auto n = gen().get_name();
    // Seed is drawn from mt19937_64, extremely unlikely to be 0.
    REQUIRE(n.seed() != 0);
}

TEST_CASE("name - implicit conversion to vector", "[name]")
{
    auto n = gen().get_name();
    n.append_surname();
    std::vector<std::wstring> parts = n;
    REQUIRE(parts.size() == 2);
    REQUIRE(parts[0] == n.parts()[0]);
    REQUIRE(parts[1] == n.parts()[1]);
}

TEST_CASE("name - streaming operator", "[name]")
{
    auto& g = gen();
    constexpr std::uint64_t seed = 777;
    auto n = g.get_name(dasmig::gender::m, dasmig::culture::american, seed);
    std::wostringstream oss;
    oss << n;
    REQUIRE(oss.str() == static_cast<std::wstring>(n));
}

TEST_CASE("name - full string equals space-joined parts", "[name]")
{
    auto n = gen().get_name();
    n.append_name().append_surname();
    std::wstring expected;
    for (const auto& part : n.parts())
    {
        if (!expected.empty()) expected += L' ';
        expected += part;
    }
    REQUIRE(static_cast<std::wstring>(n) == expected);
}

// ===========================================================================
// get_name / get_surname basics
// ===========================================================================
TEST_CASE("get_name produces names from loaded resources", "[ng]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);

    for (int i = 0; i < 50; ++i)
    {
        auto n = g.get_name();
        std::wstring s = n;
        REQUIRE_FALSE(s.empty());

        // Verify it comes from one of the loaded name databases.
        bool found = false;
        for (const auto& [culture, names] : m_db)
        {
            if (std::find(names.begin(), names.end(), s) != names.end())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (const auto& [culture, names] : f_db)
            {
                if (std::find(names.begin(), names.end(), s) != names.end())
                {
                    found = true;
                    break;
                }
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("get_surname produces surnames from loaded resources", "[ng]")
{
    auto& g = gen();
    auto& s_db = ng_test_access::surnames(g);

    for (int i = 0; i < 50; ++i)
    {
        auto n = g.get_surname();
        std::wstring s = n;
        REQUIRE_FALSE(s.empty());

        bool found = false;
        for (const auto& [culture, names] : s_db)
        {
            if (std::find(names.begin(), names.end(), s) != names.end())
            {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("get_name with specific gender picks from correct database", "[ng]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);

    // Male names — verify against male database for a fixed culture.
    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_name(dasmig::gender::m, dasmig::culture::american);
        std::wstring s = n;
        const auto& american_m = m_db.at(dasmig::culture::american);
        REQUIRE(std::find(american_m.begin(), american_m.end(), s) !=
                american_m.end());
    }

    // Female names — verify against female database for a fixed culture.
    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_name(dasmig::gender::f, dasmig::culture::american);
        std::wstring s = n;
        const auto& american_f = f_db.at(dasmig::culture::american);
        REQUIRE(std::find(american_f.begin(), american_f.end(), s) !=
                american_f.end());
    }
}

TEST_CASE("get_name with specific culture picks from that culture", "[ng]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);

    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_name(dasmig::gender::any, dasmig::culture::brazilian);
        std::wstring s = n;
        const auto& br_m = m_db.at(dasmig::culture::brazilian);
        const auto& br_f = f_db.at(dasmig::culture::brazilian);
        bool in_m = std::find(br_m.begin(), br_m.end(), s) != br_m.end();
        bool in_f = std::find(br_f.begin(), br_f.end(), s) != br_f.end();
        REQUIRE((in_m || in_f));
    }
}

TEST_CASE("get_surname with specific culture picks from that culture", "[ng]")
{
    auto& g = gen();
    auto& s_db = ng_test_access::surnames(g);

    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_surname(dasmig::culture::german);
        std::wstring s = n;
        const auto& de_sur = s_db.at(dasmig::culture::german);
        REQUIRE(std::find(de_sur.begin(), de_sur.end(), s) != de_sur.end());
    }
}

TEST_CASE("get_name with gender and culture", "[ng]")
{
    auto& g = gen();
    auto& f_db = ng_test_access::f_names(g);

    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_name(dasmig::gender::f, dasmig::culture::french);
        std::wstring s = n;
        const auto& fr_f = f_db.at(dasmig::culture::french);
        REQUIRE(std::find(fr_f.begin(), fr_f.end(), s) != fr_f.end());
    }
}

TEST_CASE("every concrete culture produces names", "[ng]")
{
    auto& g = gen();
    for (int i = 0; i < static_cast<int>(dasmig::culture::any); ++i)
    {
        auto c = static_cast<dasmig::culture>(i);
        INFO("culture index: " << i);
        auto male = g.get_name(dasmig::gender::m, c);
        REQUIRE_FALSE(static_cast<std::wstring>(male).empty());
        auto female = g.get_name(dasmig::gender::f, c);
        REQUIRE_FALSE(static_cast<std::wstring>(female).empty());
        auto sur = g.get_surname(c);
        REQUIRE_FALSE(static_cast<std::wstring>(sur).empty());
    }
}

// ===========================================================================
// append chaining
// ===========================================================================
TEST_CASE("append_name adds a second part", "[ng][chaining]")
{
    auto n = gen().get_name();
    auto& result = n.append_name();
    REQUIRE(&result == &n); // returns *this
    REQUIRE(n.parts().size() == 2);
    // Full string should contain a space.
    auto full = static_cast<std::wstring>(n);
    REQUIRE(full.find(L' ') != std::wstring::npos);
}

TEST_CASE("append_surname adds a surname part", "[ng][chaining]")
{
    auto n = gen().get_name();
    n.append_surname();
    REQUIRE(n.parts().size() == 2);
}

TEST_CASE("append_name with specific culture", "[ng][chaining]")
{
    auto n = gen().get_name(dasmig::gender::m, dasmig::culture::american);
    n.append_name(dasmig::culture::brazilian);
    REQUIRE(n.parts().size() == 2);
}

TEST_CASE("append_surname with specific culture", "[ng][chaining]")
{
    auto n = gen().get_name();
    n.append_surname(dasmig::culture::german);
    REQUIRE(n.parts().size() == 2);

    // Verify the appended surname comes from the german surname database.
    auto& s_db = ng_test_access::surnames(gen());
    const auto& de_sur = s_db.at(dasmig::culture::german);
    REQUIRE(std::find(de_sur.begin(), de_sur.end(), n.parts()[1]) !=
            de_sur.end());
}

TEST_CASE("append_name with specific culture picks from that culture",
          "[ng][chaining]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);

    auto n = g.get_name(dasmig::gender::m, dasmig::culture::american);
    n.append_name(dasmig::culture::brazilian);
    REQUIRE(n.parts().size() == 2);

    // Second part should come from the brazilian male or female database.
    const auto& br_m = m_db.at(dasmig::culture::brazilian);
    const auto& br_f = f_db.at(dasmig::culture::brazilian);
    bool in_m = std::find(br_m.begin(), br_m.end(), n.parts()[1]) !=
                br_m.end();
    bool in_f = std::find(br_f.begin(), br_f.end(), n.parts()[1]) !=
                br_f.end();
    REQUIRE((in_m || in_f));
}

TEST_CASE("triple chaining works", "[ng][chaining]")
{
    auto n = gen().get_name();
    n.append_name().append_surname();
    REQUIRE(n.parts().size() == 3);
}

// ===========================================================================
// to_culture / to_gender
// ===========================================================================
TEST_CASE("to_culture maps all known codes", "[ng][utility]")
{
    REQUIRE(dasmig::ng::to_culture(L"ar") == dasmig::culture::argentinian);
    REQUIRE(dasmig::ng::to_culture(L"us") == dasmig::culture::american);
    REQUIRE(dasmig::ng::to_culture(L"au") == dasmig::culture::australian);
    REQUIRE(dasmig::ng::to_culture(L"br") == dasmig::culture::brazilian);
    REQUIRE(dasmig::ng::to_culture(L"gb") == dasmig::culture::british);
    REQUIRE(dasmig::ng::to_culture(L"bg") == dasmig::culture::bulgarian);
    REQUIRE(dasmig::ng::to_culture(L"ca") == dasmig::culture::canadian);
    REQUIRE(dasmig::ng::to_culture(L"cn") == dasmig::culture::chinese);
    REQUIRE(dasmig::ng::to_culture(L"dk") == dasmig::culture::danish);
    REQUIRE(dasmig::ng::to_culture(L"fi") == dasmig::culture::finnish);
    REQUIRE(dasmig::ng::to_culture(L"fr") == dasmig::culture::french);
    REQUIRE(dasmig::ng::to_culture(L"de") == dasmig::culture::german);
    REQUIRE(dasmig::ng::to_culture(L"kz") == dasmig::culture::kazakh);
    REQUIRE(dasmig::ng::to_culture(L"mx") == dasmig::culture::mexican);
    REQUIRE(dasmig::ng::to_culture(L"no") == dasmig::culture::norwegian);
    REQUIRE(dasmig::ng::to_culture(L"pl") == dasmig::culture::polish);
    REQUIRE(dasmig::ng::to_culture(L"pt") == dasmig::culture::portuguese);
    REQUIRE(dasmig::ng::to_culture(L"ru") == dasmig::culture::russian);
    REQUIRE(dasmig::ng::to_culture(L"es") == dasmig::culture::spanish);
    REQUIRE(dasmig::ng::to_culture(L"se") == dasmig::culture::swedish);
    REQUIRE(dasmig::ng::to_culture(L"tr") == dasmig::culture::turkish);
    REQUIRE(dasmig::ng::to_culture(L"ua") == dasmig::culture::ukrainian);
}

TEST_CASE("to_culture returns any for unknown code", "[ng][utility]")
{
    REQUIRE(dasmig::ng::to_culture(L"xx") == dasmig::culture::any);
    REQUIRE(dasmig::ng::to_culture(L"") == dasmig::culture::any);
}

TEST_CASE("to_gender maps known strings", "[ng][utility]")
{
    REQUIRE(dasmig::ng::to_gender(L"m") == dasmig::gender::m);
    REQUIRE(dasmig::ng::to_gender(L"f") == dasmig::gender::f);
    REQUIRE(dasmig::ng::to_gender(L"male") == dasmig::gender::m);
    REQUIRE(dasmig::ng::to_gender(L"female") == dasmig::gender::f);
}

TEST_CASE("to_gender returns any for unknown string", "[ng][utility]")
{
    REQUIRE(dasmig::ng::to_gender(L"other") == dasmig::gender::any);
    REQUIRE(dasmig::ng::to_gender(L"") == dasmig::gender::any);
}

// ===========================================================================
// has_resources
// ===========================================================================
TEST_CASE("has_resources returns true after loading", "[ng]")
{
    REQUIRE(gen().has_resources());
}

TEST_CASE("has_resources returns false on empty instance", "[ng]")
{
    dasmig::ng empty;
    REQUIRE_FALSE(empty.has_resources());
}

// ===========================================================================
// all cultures load
// ===========================================================================
TEST_CASE("all cultures have male, female, and surname data", "[ng][resources]")
{
    auto& g = gen();
    auto& m = ng_test_access::m_names(g);
    auto& f = ng_test_access::f_names(g);
    auto& s = ng_test_access::surnames(g);

    // All 23 concrete cultures should be loaded.
    for (int i = 0; i < static_cast<int>(dasmig::culture::any); ++i)
    {
        auto c = static_cast<dasmig::culture>(i);
        INFO("culture index: " << i);
        REQUIRE(m.contains(c));
        REQUIRE(f.contains(c));
        REQUIRE(s.contains(c));
        REQUIRE_FALSE(m.at(c).empty());
        REQUIRE_FALSE(f.at(c).empty());
        REQUIRE_FALSE(s.at(c).empty());
    }
}

// ===========================================================================
// deterministic seeding
// ===========================================================================
TEST_CASE("seeded get_name is deterministic", "[ng][seeding]")
{
    auto& g = gen();
    constexpr std::uint64_t seed = 42;

    auto a = g.get_name(dasmig::gender::m, dasmig::culture::american, seed);
    auto b = g.get_name(dasmig::gender::m, dasmig::culture::american, seed);
    REQUIRE(static_cast<std::wstring>(a) == static_cast<std::wstring>(b));
    REQUIRE(a.seed() == seed);
    REQUIRE(b.seed() == seed);
}

TEST_CASE("seeded get_surname is deterministic", "[ng][seeding]")
{
    auto& g = gen();
    constexpr std::uint64_t seed = 99;

    auto a = g.get_surname(dasmig::culture::brazilian, seed);
    auto b = g.get_surname(dasmig::culture::brazilian, seed);
    REQUIRE(static_cast<std::wstring>(a) == static_cast<std::wstring>(b));
    REQUIRE(a.seed() == seed);
    REQUIRE(b.seed() == seed);
}

TEST_CASE("seeded name exists in the correct database", "[ng][seeding]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    constexpr std::uint64_t seed = 555;

    auto n = g.get_name(dasmig::gender::m, dasmig::culture::german, seed);
    std::wstring s = n;
    const auto& de_m = m_db.at(dasmig::culture::german);
    REQUIRE(std::find(de_m.begin(), de_m.end(), s) != de_m.end());
}

TEST_CASE("seeded surname exists in the correct database", "[ng][seeding]")
{
    auto& g = gen();
    auto& s_db = ng_test_access::surnames(g);
    constexpr std::uint64_t seed = 666;

    auto n = g.get_surname(dasmig::culture::french, seed);
    std::wstring s = n;
    const auto& fr_sur = s_db.at(dasmig::culture::french);
    REQUIRE(std::find(fr_sur.begin(), fr_sur.end(), s) != fr_sur.end());
}

TEST_CASE("name::seed() replay produces identical name", "[ng][seeding]")
{
    auto& g = gen();
    auto original = g.get_name(dasmig::gender::m, dasmig::culture::american);
    auto replay = g.get_name(dasmig::gender::m, dasmig::culture::american,
                              original.seed());
    REQUIRE(static_cast<std::wstring>(original) ==
            static_cast<std::wstring>(replay));
}

TEST_CASE("surname seed() replay produces identical surname", "[ng][seeding]")
{
    auto& g = gen();
    auto original = g.get_surname(dasmig::culture::polish);
    auto replay = g.get_surname(dasmig::culture::polish, original.seed());
    REQUIRE(static_cast<std::wstring>(original) ==
            static_cast<std::wstring>(replay));
}

TEST_CASE("different seeds produce different results", "[ng][seeding]")
{
    auto& g = gen();

    bool found_different = false;
    auto baseline = static_cast<std::wstring>(
        g.get_name(dasmig::gender::m, dasmig::culture::american, 1));

    for (std::uint64_t s = 2; s <= 100; ++s)
    {
        auto other = static_cast<std::wstring>(
            g.get_name(dasmig::gender::m, dasmig::culture::american, s));
        if (other != baseline)
        {
            found_different = true;
            break;
        }
    }
    REQUIRE(found_different);
}

TEST_CASE("name::seed() returns the seed used", "[ng][seeding]")
{
    auto& g = gen();
    constexpr std::uint64_t seed = 12345;

    auto n = g.get_name(dasmig::gender::any, dasmig::culture::any, seed);
    REQUIRE(n.seed() == seed);
}

TEST_CASE("seed() / unseed() produce deterministic sequences", "[ng][seeding]")
{
    auto& g = gen();

    g.seed(200);
    auto a1 = static_cast<std::wstring>(g.get_name());
    auto a2 = static_cast<std::wstring>(g.get_name());

    g.seed(200);
    auto b1 = static_cast<std::wstring>(g.get_name());
    auto b2 = static_cast<std::wstring>(g.get_name());

    REQUIRE(a1 == b1);
    REQUIRE(a2 == b2);

    g.unseed();
}

TEST_CASE("seed() chaining works", "[ng][seeding]")
{
    auto& g = gen();
    REQUIRE(&g.seed(100) == &g);
    REQUIRE(&g.unseed() == &g);
}

// ===========================================================================
// multi-instance
// ===========================================================================
TEST_CASE("independent instance loads and generates", "[ng][multi-instance]")
{
    dasmig::ng g;
    REQUIRE_FALSE(g.has_resources());

    g.load(resource_path());
    REQUIRE(g.has_resources());

    auto n = g.get_name();
    REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
}

TEST_CASE("independent instances have separate resources",
          "[ng][multi-instance]")
{
    dasmig::ng a;
    dasmig::ng b;

    a.load(resource_path());
    REQUIRE(a.has_resources());
    REQUIRE_FALSE(b.has_resources());
}

TEST_CASE("independent instances have separate seeding",
          "[ng][multi-instance]")
{
    dasmig::ng a;
    dasmig::ng b;
    a.load(resource_path());
    b.load(resource_path());

    a.seed(111);
    b.seed(222);

    auto na = static_cast<std::wstring>(a.get_name());
    auto nb = static_cast<std::wstring>(b.get_name());

    a.seed(111);
    b.seed(222);

    auto na2 = static_cast<std::wstring>(a.get_name());
    auto nb2 = static_cast<std::wstring>(b.get_name());

    REQUIRE(na == na2);
    REQUIRE(nb == nb2);
}

TEST_CASE("moved instance retains state", "[ng][multi-instance]")
{
    dasmig::ng src;
    src.load(resource_path());
    src.seed(42);
    auto expected = static_cast<std::wstring>(src.get_name());

    src.seed(42);
    dasmig::ng dst = std::move(src);
    auto actual = static_cast<std::wstring>(dst.get_name());

    REQUIRE(expected == actual);
}

TEST_CASE("singleton still works", "[ng][multi-instance]")
{
    auto& g = dasmig::ng::instance();
    auto n = g.get_name();
    REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
}

// ===========================================================================
// error handling
// ===========================================================================
TEST_CASE("get_name on empty instance throws", "[ng][error]")
{
    dasmig::ng empty;
    REQUIRE_THROWS_AS(empty.get_name(dasmig::gender::m,
                                      dasmig::culture::american),
                      std::invalid_argument);
}

TEST_CASE("get_name error message includes culture and gender", "[ng][error]")
{
    dasmig::ng empty;
    try
    {
        static_cast<void>(
            empty.get_name(dasmig::gender::m, dasmig::culture::american));
        FAIL("expected std::invalid_argument");
    }
    catch (const std::invalid_argument& ex)
    {
        std::string msg = ex.what();
        REQUIRE(msg.find("male") != std::string::npos);
        REQUIRE(msg.find("american") != std::string::npos);
    }
}

TEST_CASE("get_surname on empty instance throws", "[ng][error]")
{
    dasmig::ng empty;
    REQUIRE_THROWS_AS(empty.get_surname(dasmig::culture::american),
                      std::invalid_argument);
}

TEST_CASE("get_surname error message includes culture", "[ng][error]")
{
    dasmig::ng empty;
    try
    {
        static_cast<void>(
            empty.get_surname(dasmig::culture::american));
        FAIL("expected std::invalid_argument");
    }
    catch (const std::invalid_argument& ex)
    {
        std::string msg = ex.what();
        REQUIRE(msg.find("american") != std::string::npos);
    }
}

TEST_CASE("load with non-existent path is a no-op", "[ng][error]")
{
    dasmig::ng g;
    g.load("this_path_does_not_exist_at_all");
    REQUIRE_FALSE(g.has_resources());
}

// ===========================================================================
// UTF-8 decoder
// ===========================================================================
TEST_CASE("utf8_to_wstring decodes ASCII", "[ng][utf8]")
{
    REQUIRE(ng_test_access::utf8_to_wstring("Hello") == L"Hello");
    REQUIRE(ng_test_access::utf8_to_wstring("") == L"");
}

TEST_CASE("utf8_to_wstring decodes 2-byte sequences", "[ng][utf8]")
{
    // "ü" = U+00FC = 0xC3 0xBC
    std::string input{'\xC3', '\xBC'};
    REQUIRE(ng_test_access::utf8_to_wstring(input) == L"\u00FC");
}

TEST_CASE("utf8_to_wstring decodes 3-byte sequences", "[ng][utf8]")
{
    // "€" = U+20AC = 0xE2 0x82 0xAC
    std::string input{'\xE2', '\x82', '\xAC'};
    REQUIRE(ng_test_access::utf8_to_wstring(input) == L"\u20AC");
}

TEST_CASE("utf8_to_wstring decodes 4-byte sequences", "[ng][utf8]")
{
    // U+1F600 (grinning face) = 0xF0 0x9F 0x98 0x80
    std::string input{'\xF0', '\x9F', '\x98', '\x80'};
    auto result = ng_test_access::utf8_to_wstring(input);
    REQUIRE_FALSE(result.empty());
    // Verify round-trip: on 4-byte wchar_t (Linux) it's one char,
    // on 2-byte wchar_t (Windows) it's a surrogate pair of 2 chars.
    if constexpr (sizeof(wchar_t) >= 4)
    {
        REQUIRE(result.size() == 1);
        REQUIRE(static_cast<char32_t>(result[0]) == 0x1F600U);
    }
    else
    {
        REQUIRE(result.size() == 2);
    }
}

TEST_CASE("utf8_to_wstring skips invalid lead bytes", "[ng][utf8]")
{
    // 0xFF is never valid UTF-8; surrounding ASCII should survive.
    std::string input{'A', '\xFF', 'B'};
    REQUIRE(ng_test_access::utf8_to_wstring(input) == L"AB");
}

TEST_CASE("utf8_to_wstring handles truncated sequence", "[ng][utf8]")
{
    // 0xC3 expects one continuation byte but string ends.
    std::string input{'\xC3'};
    REQUIRE(ng_test_access::utf8_to_wstring(input) == L"");
}

TEST_CASE("utf8_to_wstring rejects invalid continuation byte", "[ng][utf8]")
{
    // 0xC3 expects continuation (10xxxxxx) but gets 0x41 ('A').
    std::string input{'\xC3', 'A', 'B'};
    auto result = ng_test_access::utf8_to_wstring(input);
    // The malformed 2-byte sequence is skipped; 'A' is consumed as
    // continuation so only 'B' survives.
    REQUIRE(result.find(L'B') != std::wstring::npos);
}

TEST_CASE("utf8_to_wstring rejects overlong codepoint > U+10FFFF",
          "[ng][utf8]")
{
    // Fabricate a 4-byte sequence that decodes to U+110000 (invalid).
    // F4 90 80 80 = U+110000
    std::string input{'\xF4', '\x90', '\x80', '\x80'};
    REQUIRE(ng_test_access::utf8_to_wstring(input) == L"");
}

TEST_CASE("utf8_to_wstring decodes real name Müller", "[ng][utf8]")
{
    REQUIRE(ng_test_access::utf8_to_wstring("M\xC3\xBCller") == L"M\u00FCller");
}

TEST_CASE("utf8_to_wstring decodes real name João", "[ng][utf8]")
{
    REQUIRE(ng_test_access::utf8_to_wstring("Jo\xC3\xA3o") == L"Jo\u00E3o");
}

// ===========================================================================
// random culture coverage (statistical)
// ===========================================================================
TEST_CASE("random culture covers multiple cultures", "[ng][random]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);
    std::set<dasmig::culture> cultures_hit;

    for (int i = 0; i < 1000; ++i)
    {
        auto n = g.get_name();
        std::wstring s = n;
        // Identify which culture this name belongs to.
        for (const auto& [c, names] : m_db)
        {
            if (std::find(names.begin(), names.end(), s) != names.end())
            {
                cultures_hit.insert(c);
                break;
            }
        }
        for (const auto& [c, names] : f_db)
        {
            if (std::find(names.begin(), names.end(), s) != names.end())
            {
                cultures_hit.insert(c);
                break;
            }
        }
    }

    // With 1000 random names across 23 cultures, we should hit many.
    REQUIRE(cultures_hit.size() > 10);
}
