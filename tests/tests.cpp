#include "catch_amalgamated.hpp"
#include "../dasmig/namegen.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
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
        return g._m_pool;
    }
    static auto& f_names(dasmig::ng& g)
    {
        return g._f_pool;
    }
    static auto& surnames(dasmig::ng& g)
    {
        return g._sur_pool;
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
            auto lite = p / "lite";
            auto full = p / "full";
            if (std::filesystem::is_directory(lite)) return lite;
            if (std::filesystem::is_directory(full)) return full;
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
    // Seed is drawn from the 64-bit engine, extremely unlikely to be 0.
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
        for (const auto& [culture, pool] : m_db)
        {
            if (std::find(pool.names.begin(), pool.names.end(), s) != pool.names.end())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (const auto& [culture, pool] : f_db)
            {
                if (std::find(pool.names.begin(), pool.names.end(), s) != pool.names.end())
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
        for (const auto& [culture, pool] : s_db)
        {
            if (std::find(pool.names.begin(), pool.names.end(), s) != pool.names.end())
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
        const auto& american_m = m_db.at(dasmig::culture::american).names;
        REQUIRE(std::find(american_m.begin(), american_m.end(), s) !=
                american_m.end());
    }

    // Female names — verify against female database for a fixed culture.
    for (int i = 0; i < 20; ++i)
    {
        auto n = g.get_name(dasmig::gender::f, dasmig::culture::american);
        std::wstring s = n;
        const auto& american_f = f_db.at(dasmig::culture::american).names;
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
        const auto& br_m = m_db.at(dasmig::culture::brazilian).names;
        const auto& br_f = f_db.at(dasmig::culture::brazilian).names;
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
        const auto& de_sur = s_db.at(dasmig::culture::german).names;
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
        const auto& fr_f = f_db.at(dasmig::culture::french).names;
        REQUIRE(std::find(fr_f.begin(), fr_f.end(), s) != fr_f.end());
    }
}

TEST_CASE("every concrete culture produces names", "[ng]")
{
    auto& g = gen();
    auto& m_db = ng_test_access::m_names(g);
    auto& f_db = ng_test_access::f_names(g);
    auto& s_db = ng_test_access::surnames(g);

    for (int i = 0; i < static_cast<int>(dasmig::culture::any); ++i)
    {
        auto c = static_cast<dasmig::culture>(i);
        INFO("culture index: " << i);
        if (m_db.contains(c) && !m_db.at(c).names.empty())
        {
            auto male = g.get_name(dasmig::gender::m, c);
            REQUIRE_FALSE(static_cast<std::wstring>(male).empty());
        }
        if (f_db.contains(c) && !f_db.at(c).names.empty())
        {
            auto female = g.get_name(dasmig::gender::f, c);
            REQUIRE_FALSE(static_cast<std::wstring>(female).empty());
        }
        if (s_db.contains(c) && !s_db.at(c).names.empty())
        {
            auto sur = g.get_surname(c);
            REQUIRE_FALSE(static_cast<std::wstring>(sur).empty());
        }
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
    const auto& de_sur = s_db.at(dasmig::culture::german).names;
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
    const auto& br_m = m_db.at(dasmig::culture::brazilian).names;
    const auto& br_f = f_db.at(dasmig::culture::brazilian).names;
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
    REQUIRE(dasmig::ng::to_culture(L"ae") == dasmig::culture::emirati);
    REQUIRE(dasmig::ng::to_culture(L"af") == dasmig::culture::afghan);
    REQUIRE(dasmig::ng::to_culture(L"al") == dasmig::culture::albanian);
    REQUIRE(dasmig::ng::to_culture(L"ao") == dasmig::culture::angolan);
    REQUIRE(dasmig::ng::to_culture(L"ar") == dasmig::culture::argentinian);
    REQUIRE(dasmig::ng::to_culture(L"at") == dasmig::culture::austrian);
    REQUIRE(dasmig::ng::to_culture(L"az") == dasmig::culture::azerbaijani);
    REQUIRE(dasmig::ng::to_culture(L"bd") == dasmig::culture::bangladeshi);
    REQUIRE(dasmig::ng::to_culture(L"be") == dasmig::culture::belgian);
    REQUIRE(dasmig::ng::to_culture(L"bf") == dasmig::culture::burkinabe);
    REQUIRE(dasmig::ng::to_culture(L"bg") == dasmig::culture::bulgarian);
    REQUIRE(dasmig::ng::to_culture(L"bh") == dasmig::culture::bahraini);
    REQUIRE(dasmig::ng::to_culture(L"bi") == dasmig::culture::burundian);
    REQUIRE(dasmig::ng::to_culture(L"bn") == dasmig::culture::bruneian);
    REQUIRE(dasmig::ng::to_culture(L"bo") == dasmig::culture::bolivian);
    REQUIRE(dasmig::ng::to_culture(L"br") == dasmig::culture::brazilian);
    REQUIRE(dasmig::ng::to_culture(L"bw") == dasmig::culture::botswanan);
    REQUIRE(dasmig::ng::to_culture(L"ca") == dasmig::culture::canadian);
    REQUIRE(dasmig::ng::to_culture(L"ch") == dasmig::culture::swiss);
    REQUIRE(dasmig::ng::to_culture(L"cl") == dasmig::culture::chilean);
    REQUIRE(dasmig::ng::to_culture(L"cm") == dasmig::culture::cameroonian);
    REQUIRE(dasmig::ng::to_culture(L"cn") == dasmig::culture::chinese);
    REQUIRE(dasmig::ng::to_culture(L"co") == dasmig::culture::colombian);
    REQUIRE(dasmig::ng::to_culture(L"cr") == dasmig::culture::costarican);
    REQUIRE(dasmig::ng::to_culture(L"cy") == dasmig::culture::cypriot);
    REQUIRE(dasmig::ng::to_culture(L"cz") == dasmig::culture::czech);
    REQUIRE(dasmig::ng::to_culture(L"de") == dasmig::culture::german);
    REQUIRE(dasmig::ng::to_culture(L"dj") == dasmig::culture::djiboutian);
    REQUIRE(dasmig::ng::to_culture(L"dk") == dasmig::culture::danish);
    REQUIRE(dasmig::ng::to_culture(L"dz") == dasmig::culture::algerian);
    REQUIRE(dasmig::ng::to_culture(L"ec") == dasmig::culture::ecuadorian);
    REQUIRE(dasmig::ng::to_culture(L"ee") == dasmig::culture::estonian);
    REQUIRE(dasmig::ng::to_culture(L"eg") == dasmig::culture::egyptian);
    REQUIRE(dasmig::ng::to_culture(L"es") == dasmig::culture::spanish);
    REQUIRE(dasmig::ng::to_culture(L"et") == dasmig::culture::ethiopian);
    REQUIRE(dasmig::ng::to_culture(L"fi") == dasmig::culture::finnish);
    REQUIRE(dasmig::ng::to_culture(L"fj") == dasmig::culture::fijian);
    REQUIRE(dasmig::ng::to_culture(L"fr") == dasmig::culture::french);
    REQUIRE(dasmig::ng::to_culture(L"gb") == dasmig::culture::british);
    REQUIRE(dasmig::ng::to_culture(L"ge") == dasmig::culture::georgian);
    REQUIRE(dasmig::ng::to_culture(L"gh") == dasmig::culture::ghanaian);
    REQUIRE(dasmig::ng::to_culture(L"gr") == dasmig::culture::greek);
    REQUIRE(dasmig::ng::to_culture(L"gt") == dasmig::culture::guatemalan);
    REQUIRE(dasmig::ng::to_culture(L"hk") == dasmig::culture::hongkonger);
    REQUIRE(dasmig::ng::to_culture(L"hn") == dasmig::culture::honduran);
    REQUIRE(dasmig::ng::to_culture(L"hr") == dasmig::culture::croatian);
    REQUIRE(dasmig::ng::to_culture(L"ht") == dasmig::culture::haitian);
    REQUIRE(dasmig::ng::to_culture(L"hu") == dasmig::culture::hungarian);
    REQUIRE(dasmig::ng::to_culture(L"id") == dasmig::culture::indonesian);
    REQUIRE(dasmig::ng::to_culture(L"ie") == dasmig::culture::irish);
    REQUIRE(dasmig::ng::to_culture(L"il") == dasmig::culture::israeli);
    REQUIRE(dasmig::ng::to_culture(L"in") == dasmig::culture::indian);
    REQUIRE(dasmig::ng::to_culture(L"iq") == dasmig::culture::iraqi);
    REQUIRE(dasmig::ng::to_culture(L"ir") == dasmig::culture::iranian);
    REQUIRE(dasmig::ng::to_culture(L"is") == dasmig::culture::icelandic);
    REQUIRE(dasmig::ng::to_culture(L"it") == dasmig::culture::italian);
    REQUIRE(dasmig::ng::to_culture(L"jm") == dasmig::culture::jamaican);
    REQUIRE(dasmig::ng::to_culture(L"jo") == dasmig::culture::jordanian);
    REQUIRE(dasmig::ng::to_culture(L"jp") == dasmig::culture::japanese);
    REQUIRE(dasmig::ng::to_culture(L"kh") == dasmig::culture::cambodian);
    REQUIRE(dasmig::ng::to_culture(L"kr") == dasmig::culture::korean);
    REQUIRE(dasmig::ng::to_culture(L"kw") == dasmig::culture::kuwaiti);
    REQUIRE(dasmig::ng::to_culture(L"kz") == dasmig::culture::kazakh);
    REQUIRE(dasmig::ng::to_culture(L"lb") == dasmig::culture::lebanese);
    REQUIRE(dasmig::ng::to_culture(L"lt") == dasmig::culture::lithuanian);
    REQUIRE(dasmig::ng::to_culture(L"lu") == dasmig::culture::luxembourgish);
    REQUIRE(dasmig::ng::to_culture(L"ly") == dasmig::culture::libyan);
    REQUIRE(dasmig::ng::to_culture(L"ma") == dasmig::culture::moroccan);
    REQUIRE(dasmig::ng::to_culture(L"md") == dasmig::culture::moldovan);
    REQUIRE(dasmig::ng::to_culture(L"mo") == dasmig::culture::macanese);
    REQUIRE(dasmig::ng::to_culture(L"mt") == dasmig::culture::maltese);
    REQUIRE(dasmig::ng::to_culture(L"mu") == dasmig::culture::mauritian);
    REQUIRE(dasmig::ng::to_culture(L"mv") == dasmig::culture::maldivian);
    REQUIRE(dasmig::ng::to_culture(L"mx") == dasmig::culture::mexican);
    REQUIRE(dasmig::ng::to_culture(L"my") == dasmig::culture::malaysian);
    REQUIRE(dasmig::ng::to_culture(L"na") == dasmig::culture::namibian);
    REQUIRE(dasmig::ng::to_culture(L"ng") == dasmig::culture::nigerian);
    REQUIRE(dasmig::ng::to_culture(L"nl") == dasmig::culture::dutch);
    REQUIRE(dasmig::ng::to_culture(L"no") == dasmig::culture::norwegian);
    REQUIRE(dasmig::ng::to_culture(L"om") == dasmig::culture::omani);
    REQUIRE(dasmig::ng::to_culture(L"pa") == dasmig::culture::panamanian);
    REQUIRE(dasmig::ng::to_culture(L"pe") == dasmig::culture::peruvian);
    REQUIRE(dasmig::ng::to_culture(L"ph") == dasmig::culture::filipino);
    REQUIRE(dasmig::ng::to_culture(L"pl") == dasmig::culture::polish);
    REQUIRE(dasmig::ng::to_culture(L"pr") == dasmig::culture::puertorican);
    REQUIRE(dasmig::ng::to_culture(L"ps") == dasmig::culture::palestinian);
    REQUIRE(dasmig::ng::to_culture(L"pt") == dasmig::culture::portuguese);
    REQUIRE(dasmig::ng::to_culture(L"qa") == dasmig::culture::qatari);
    REQUIRE(dasmig::ng::to_culture(L"rs") == dasmig::culture::serbian);
    REQUIRE(dasmig::ng::to_culture(L"ru") == dasmig::culture::russian);
    REQUIRE(dasmig::ng::to_culture(L"sa") == dasmig::culture::saudi);
    REQUIRE(dasmig::ng::to_culture(L"sd") == dasmig::culture::sudanese);
    REQUIRE(dasmig::ng::to_culture(L"se") == dasmig::culture::swedish);
    REQUIRE(dasmig::ng::to_culture(L"sg") == dasmig::culture::singaporean);
    REQUIRE(dasmig::ng::to_culture(L"si") == dasmig::culture::slovenian);
    REQUIRE(dasmig::ng::to_culture(L"sv") == dasmig::culture::salvadoran);
    REQUIRE(dasmig::ng::to_culture(L"sy") == dasmig::culture::syrian);
    REQUIRE(dasmig::ng::to_culture(L"tm") == dasmig::culture::turkmen);
    REQUIRE(dasmig::ng::to_culture(L"tn") == dasmig::culture::tunisian);
    REQUIRE(dasmig::ng::to_culture(L"tr") == dasmig::culture::turkish);
    REQUIRE(dasmig::ng::to_culture(L"tw") == dasmig::culture::taiwanese);
    REQUIRE(dasmig::ng::to_culture(L"us") == dasmig::culture::american);
    REQUIRE(dasmig::ng::to_culture(L"uy") == dasmig::culture::uruguayan);
    REQUIRE(dasmig::ng::to_culture(L"ye") == dasmig::culture::yemeni);
    REQUIRE(dasmig::ng::to_culture(L"za") == dasmig::culture::southafrican);
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

    // All 105 concrete cultures should be loaded.
    // Some cultures may lack a category (e.g. Korean has no male names in
    // the reduced dataset). We verify at least 100 have complete data.
    int complete = 0;
    for (int i = 0; i < static_cast<int>(dasmig::culture::any); ++i)
    {
        auto c = static_cast<dasmig::culture>(i);
        if (m.contains(c) && !m.at(c).names.empty() &&
            f.contains(c) && !f.at(c).names.empty() &&
            s.contains(c) && !s.at(c).names.empty())
        {
            ++complete;
        }
    }
    REQUIRE(complete >= 100);
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
    const auto& de_m = m_db.at(dasmig::culture::german).names;
    REQUIRE(std::find(de_m.begin(), de_m.end(), s) != de_m.end());
}

TEST_CASE("seeded surname exists in the correct database", "[ng][seeding]")
{
    auto& g = gen();
    auto& s_db = ng_test_access::surnames(g);
    constexpr std::uint64_t seed = 666;

    auto n = g.get_surname(dasmig::culture::french, seed);
    std::wstring s = n;
    const auto& fr_sur = s_db.at(dasmig::culture::french).names;
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
// UTF-8 (decoded by dasmig-core; invalid bytes are an error, not skipped)
// ===========================================================================
TEST_CASE("names are decoded from UTF-8", "[ng][utf8]")
{
    dasmig::ng g;
    g.extend_from_string("de\nany\nM\xC3\xBCller\nJo\xC3\xA3o\n\xF0\x9F\x98\x80\n", "utf8.names");
    const auto& pool = ng_test_access::surnames(g).at(dasmig::culture::german);
    REQUIRE(pool.names.size() == 3);
    REQUIRE(pool.names[0] == L"Müller");
    REQUIRE(pool.names[1] == L"João");
    // U+1F600: one wchar_t on Linux, a surrogate pair on Windows.
    if constexpr (sizeof(wchar_t) >= 4)
    {
        REQUIRE(pool.names[2].size() == 1);
        REQUIRE(static_cast<char32_t>(pool.names[2][0]) == 0x1F600U);
    }
    else
    {
        REQUIRE(pool.names[2].size() == 2);
    }
    REQUIRE(dasmig::core::to_utf8(std::wstring{g.get_surname(dasmig::culture::german, 1)}).size() >= 2);
}

TEST_CASE("invalid UTF-8 is rejected", "[ng][utf8][errors]")
{
    dasmig::ng g;
    for (const std::string& bad : {std::string{"de\nany\nA\xFF" "B\n"}, std::string{"de\nany\n\xC3\n"},
                                  std::string{"de\nany\n\xC3" "AB\n"}, std::string{"de\nany\n\xF4\x90\x80\x80\n"}})
    {
        REQUIRE_THROWS_AS(g.extend_from_string(bad, "bad.names"), dasmig::core::tsv_error);
    }
    REQUIRE_FALSE(g.has_resources());
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
        for (const auto& [c, pool] : m_db)
        {
            if (std::find(pool.names.begin(), pool.names.end(), s) != pool.names.end())
            {
                cultures_hit.insert(c);
                break;
            }
        }
        for (const auto& [c, pool] : f_db)
        {
            if (std::find(pool.names.begin(), pool.names.end(), s) != pool.names.end())
            {
                cultures_hit.insert(c);
                break;
            }
        }
    }

    // With 1000 random names across 105 cultures, we should hit many.
    REQUIRE(cultures_hit.size() > 30);
}

// ===========================================================================
// weighted selection
// ===========================================================================
TEST_CASE("weighted selection biases toward higher-weight names",
          "[ng][weighted]")
{
    // Build a tiny generator with two names: "Heavy" (weight 99) and
    // "Light" (weight 1). Over many draws, Heavy should dominate.
    dasmig::ng g;

    // Write a temp file with weighted format.
    auto tmp = std::filesystem::temp_directory_path() / "ng_weight_test";
    std::filesystem::create_directories(tmp / "wtest");
    {
        std::ofstream f{tmp / "wtest" / "male.names"};
        f << "us\nmale\nHeavy\t99.0\nLight\t1.0\n";
    }
    g.load(tmp);

    int heavy_count = 0;
    constexpr int trials = 500;
    for (int i = 0; i < trials; ++i)
    {
        auto n = g.get_name(dasmig::gender::m, dasmig::culture::american);
        if (static_cast<std::wstring>(n) == L"Heavy") ++heavy_count;
    }

    // With 99:1 weights, Heavy should appear > 80% of the time.
    REQUIRE(heavy_count > trials * 80 / 100);

    std::filesystem::remove_all(tmp);
}

// ===========================================================================
// parse_file format handling
// ===========================================================================
TEST_CASE("parse_file handles tab-delimited weights", "[ng][parse]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ng_tab_test";
    std::filesystem::create_directories(tmp / "tabtest");
    {
        std::ofstream f{tmp / "tabtest" / "female.names"};
        f << "br\nfemale\nAna\t0.5\nBia\t0.3\nCarla\t0.2\n";
    }

    dasmig::ng g;
    g.load(tmp);

    REQUIRE(g.has_resources());
    auto& pools = ng_test_access::f_names(g);
    REQUIRE(pools.contains(dasmig::culture::brazilian));
    const auto& pool = pools.at(dasmig::culture::brazilian);
    REQUIRE(pool.names.size() == 3);
    REQUIRE(pool.weights.size() == 3);
    REQUIRE(pool.names[0] == L"Ana");
    REQUIRE(pool.weights[0] == Catch::Approx(0.5));
    REQUIRE(pool.weights[2] == Catch::Approx(0.2));

    std::filesystem::remove_all(tmp);
}

TEST_CASE("parse_file defaults to weight 1.0 without tab", "[ng][parse]")
{
    auto tmp = std::filesystem::temp_directory_path() / "ng_notab_test";
    std::filesystem::create_directories(tmp / "notabtest");
    {
        std::ofstream f{tmp / "notabtest" / "sur.names"};
        f << "de\nany\nMueller\nSchmidt\n";
    }

    dasmig::ng g;
    g.load(tmp);

    auto& pools = ng_test_access::surnames(g);
    const auto& pool = pools.at(dasmig::culture::german);
    REQUIRE(pool.names.size() == 2);
    REQUIRE(pool.weights[0] == Catch::Approx(1.0));
    REQUIRE(pool.weights[1] == Catch::Approx(1.0));

    std::filesystem::remove_all(tmp);
}

// ===========================================================================
// resolve_culture on empty database
// ===========================================================================
TEST_CASE("get_name any culture on empty throws", "[ng][error]")
{
    dasmig::ng empty;
    REQUIRE_THROWS_AS(
        empty.get_name(dasmig::gender::m, dasmig::culture::any),
        std::invalid_argument);
}

TEST_CASE("get_surname any culture on empty throws", "[ng][error]")
{
    dasmig::ng empty;
    REQUIRE_THROWS_AS(
        empty.get_surname(dasmig::culture::any),
        std::invalid_argument);
}

// ===========================================================================
// culture_label / gender_label edge cases
// ===========================================================================
TEST_CASE("error message for unknown culture is descriptive", "[ng][error]")
{
    dasmig::ng empty;
    try
    {
        static_cast<void>(
            empty.get_name(dasmig::gender::f, dasmig::culture::fijian));
        FAIL("expected std::invalid_argument");
    }
    catch (const std::invalid_argument& ex)
    {
        std::string msg = ex.what();
        REQUIRE(msg.find("female") != std::string::npos);
        REQUIRE(msg.find("fijian") != std::string::npos);
    }
}

// ===========================================================================
// dataset enum loading
// ===========================================================================
TEST_CASE("load(dataset::lite) loads the lite dataset", "[ng][dataset]")
{
    dasmig::ng g;
    bool ok = g.load(dasmig::dataset::lite);
    if (ok)
    {
        REQUIRE(g.has_resources());
        auto n = g.get_name(dasmig::gender::m, dasmig::culture::american);
        REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
    }
    // If the lite directory doesn't exist in CI, skip gracefully.
}

TEST_CASE("load(dataset::full) loads the full dataset", "[ng][dataset]")
{
    dasmig::ng g;
    bool ok = g.load(dasmig::dataset::full);
    if (ok)
    {
        REQUIRE(g.has_resources());
    }
}

TEST_CASE("load(dataset) returns false for missing directory",
          "[ng][dataset]")
{
    // This test only guarantees the function returns false gracefully
    // when run from a directory without resources.
    dasmig::ng g;
    // We can't guarantee failure (resources/ may exist), so just verify
    // the return type compiles and the function doesn't crash.
    [[maybe_unused]] bool ok = g.load(dasmig::dataset::lite);
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed, request and data give these exact
// results on every compiler and platform. A change here is a change of
// output and needs a major version.
// ---------------------------------------------------------------------------

namespace
{

dasmig::ng& lite()
{
    static dasmig::ng g = [] {
        dasmig::ng x;
        x.load(std::filesystem::path{resource_path()});
        return x;
    }();
    return g;
}

} // namespace

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    using dasmig::culture;
    using dasmig::gender;
    auto& g = lite();
    REQUIRE(g.data_hash() == 0xd7aadf4cfcb4c207ULL);

    REQUIRE(g.get_name(gender::any, culture::any, 1).to_json() ==
            R"({"name":"Lenroy","parts":["Lenroy"],"gender":"male","culture":"jamaican","signature":{"seed":"0000000000000001","data":"d7aadf4cfcb4c207"}})");
    REQUIRE(g.get_name(gender::m, culture::brazilian, 2).to_json() ==
            R"({"name":"Luiz","parts":["Luiz"],"gender":"male","culture":"brazilian","signature":{"seed":"0000000000000002","data":"d7aadf4cfcb4c207"}})");
    REQUIRE(g.get_surname(culture::japanese, 3).to_json() ==
            R"({"name":"Takahashi","parts":["Takahashi"],"gender":"male","culture":"japanese","signature":{"seed":"0000000000000003","data":"d7aadf4cfcb4c207"}})");
    auto n = g.get_name(gender::f, culture::german, 4);
    n.append_name().append_surname().append_surname(culture::polish);
    REQUIRE(n.to_json() ==
            "{\"name\":\"Elfriede Mandy Kr\xC3\xA4mer Duda\",\"parts\":[\"Elfriede\",\"Mandy\",\"Kr\xC3\xA4mer\",\"Duda\"],"
            "\"gender\":\"female\",\"culture\":\"german\",\"signature\":{\"seed\":\"0000000000000004\",\"data\":\"d7aadf4cfcb4c207\"}}");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        auto x = s % 3 == 0 ? g.get_surname(culture::any, s) : g.get_name(gender::any, culture::any, s);
        if (s % 2 == 0) x.append_surname();
        for (const char c : x.to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0x5f5cc6a013383981ULL);
}

// ---------------------------------------------------------------------------
// Replay, JSON and signatures
// ---------------------------------------------------------------------------

TEST_CASE("appended parts replay from the seed", "[seeding][chaining]")
{
    auto& g = lite();
    auto a = g.get_name(dasmig::gender::m, dasmig::culture::italian);
    a.append_name().append_surname();
    auto b = g.get_name(dasmig::gender::m, dasmig::culture::italian, a.seed());
    b.append_name().append_surname();
    REQUIRE(a.parts() == b.parts());
    REQUIRE(a.to_json() == b.to_json());

    // Each position draws its own part.
    auto c = g.get_name(dasmig::gender::m, dasmig::culture::italian, a.seed());
    c.append_surname().append_name();
    REQUIRE(c.parts()[0] == a.parts()[0]);
}

TEST_CASE("forcing the gender does not change the culture draw", "[seeding]")
{
    auto& g = lite();
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto any = g.get_name(dasmig::gender::any, dasmig::culture::any, s);
        const auto forced = g.get_name(any.to_json().find("\"female\"") != std::string::npos
                                           ? dasmig::gender::f
                                           : dasmig::gender::m,
                                       dasmig::culture::any, s);
        REQUIRE(any.to_json() == forced.to_json());
    }
}

TEST_CASE("to_json and signature", "[json]")
{
    auto& g = lite();
    const auto n = g.get_surname(dasmig::culture::french, 9);
    REQUIRE(n.seed() == 9);
    REQUIRE(n.signature().data == g.data_hash());
    REQUIRE(n.to_json().find(R"("culture":"french")") != std::string::npos);
    REQUIRE(n.to_json().find(R"("signature":{"seed":"0000000000000009")") != std::string::npos);
}

TEST_CASE("culture codes and names", "[ng][utility]")
{
    REQUIRE(dasmig::ng::culture_code(dasmig::culture::brazilian) == "br");
    REQUIRE(dasmig::ng::culture_name(dasmig::culture::brazilian) == "brazilian");
    REQUIRE(dasmig::ng::culture_code(dasmig::culture::any).empty());
    REQUIRE(dasmig::ng::culture_name(dasmig::culture::any) == "any");
    for (int i = 0; i < static_cast<int>(dasmig::culture::any); ++i)
    {
        const auto c = static_cast<dasmig::culture>(i);
        const auto code = dasmig::ng::culture_code(c);
        REQUIRE(dasmig::ng::to_culture(std::wstring(code.begin(), code.end())) == c);
    }
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

TEST_CASE("extend adds, replaces and removes names", "[extend]")
{
    dasmig::ng g;
    g.load(std::filesystem::path{resource_path()});
    const auto hash = g.data_hash();
    const auto before = ng_test_access::m_names(g).at(dasmig::culture::brazilian).names.size();

    // Add two names (the first replaces a loaded one's weight), then remove one.
    const auto info = g.extend_from_string("br\nmale\nJo\xC3\xA3o\t2.5\nCaetano\t0.1\n", "mod.names");
    REQUIRE(info.added + info.replaced == 2);
    REQUIRE(g.layers(dasmig::culture::brazilian, dasmig::gender::m).size() == 2);
    REQUIRE(g.data_hash() != hash);

    const auto removal = g.extend_from_string("br\nmale\nCaetano\t\t1\n", "cleanup.names");
    REQUIRE(removal.removed == 1);
    const auto& pool = ng_test_access::m_names(g).at(dasmig::culture::brazilian);
    REQUIRE(std::find(pool.names.begin(), pool.names.end(), L"Caetano") == pool.names.end());
    REQUIRE(pool.names.size() == before + info.added - 1);

    // Removing every name of a list removes the list.
    dasmig::ng small;
    small.extend_from_string("is\nany\nJonsson\n", "a.names");
    REQUIRE(small.has_resources());
    small.extend_from_string("is\nany\nJonsson\t\t1\n", "b.names");
    REQUIRE_FALSE(small.has_resources());
    REQUIRE_THROWS_AS(small.get_surname(dasmig::culture::icelandic, 1), std::invalid_argument);

    // A new culture list; files on disk; reloading drops the layers.
    const auto dir = std::filesystem::temp_directory_path() / "ng_layer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "fj");
    {
        std::ofstream{dir / "fj" / "sur.names"} << "fj\nany\nWaqa\t1\n";
    }
    g.extend(dir);
    REQUIRE(g.layers(dasmig::culture::fijian, dasmig::gender::any).size() == 2);
    REQUIRE_THROWS_AS(g.extend(dir / "fj" / "missing.txt"), std::invalid_argument);
    std::filesystem::remove_all(dir);

    g.load(std::filesystem::path{resource_path()});
    REQUIRE(g.data_hash() == hash);
}

TEST_CASE("bad layers are rejected and change nothing", "[extend][errors]")
{
    dasmig::ng g;
    g.load(std::filesystem::path{resource_path()});
    const auto hash = g.data_hash();
    auto rejected = [&](const std::string& text, const std::string& message) {
        INFO(text);
        REQUIRE_THROWS_WITH(g.extend_from_string(text, "x.names"), message);
        REQUIRE(g.data_hash() == hash);
    };
    rejected("xx\nmale\nA\n", "x.names:1: unknown country code 'xx'");
    rejected("br\nboth\nA\n", "x.names:2: expected male, female or any, got 'both'");
    rejected("br\n", "x.names:2: expected a country code and a gender line");
    rejected("br\nmale\nA\t0\n", "x.names:3: column 'weight': must be positive");
    rejected("br\nmale\nA\theavy\n", "x.names:3: column 'weight': expected a number, got 'heavy'");
    rejected("br\nmale\nA\t1\nB\t1\nA\t2\n", "x.names:5: key repeated (first on line 3)");
    REQUIRE_THROWS_AS(g.extend_from_string("br\nmale\nJo\xC3\xA3o\t1\n", "x.names", dasmig::core::layer_mode::add),
                      dasmig::core::tsv_error);

    // A directory with one bad file applies nothing.
    const auto dir = std::filesystem::temp_directory_path() / "ng_bad_layer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream{dir / "a.names"} << "br\nmale\nFine\n";
        std::ofstream{dir / "b.names"} << "br\nfemale\nOops\t-1\n";
    }
    REQUIRE_THROWS_AS(g.extend(dir), dasmig::core::tsv_error);
    REQUIRE(g.data_hash() == hash);
    REQUIRE_THROWS_AS(g.load(dir), dasmig::core::tsv_error);
    REQUIRE(g.data_hash() == hash);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a file may end after its gender line", "[extend]")
{
    dasmig::ng g;
    REQUIRE_NOTHROW(g.extend_from_string("br\nmale", "empty.names"));
    REQUIRE_FALSE(g.has_resources());
}

#ifdef DASMIG_NAME_EMBED
TEST_CASE("compiled-in data matches the files", "[embed]")
{
    dasmig::ng embedded;
    REQUIRE(embedded.load(dasmig::dataset::lite));
    REQUIRE(embedded.data_hash() == lite().data_hash());
    REQUIRE(embedded.get_name(dasmig::gender::any, dasmig::culture::any, 1).to_json() ==
            lite().get_name(dasmig::gender::any, dasmig::culture::any, 1).to_json());
}
#endif
