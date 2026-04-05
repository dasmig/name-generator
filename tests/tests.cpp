#include "catch_amalgamated.hpp"
#include "../dasmig/namegen.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
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
    auto n = gen().get_name();
    std::wstring s = n;
    REQUIRE_FALSE(s.empty());
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
    std::vector<std::wstring> parts = n;
    REQUIRE(parts.size() >= 1);
}

TEST_CASE("name - streaming operator", "[name]")
{
    auto n = gen().get_name();
    std::wostringstream oss;
    oss << n;
    REQUIRE_FALSE(oss.str().empty());
}

// ===========================================================================
// get_name / get_surname basics
// ===========================================================================
TEST_CASE("get_name produces non-empty names", "[ng]")
{
    for (int i = 0; i < 100; ++i)
    {
        auto n = gen().get_name();
        REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
    }
}

TEST_CASE("get_surname produces non-empty surnames", "[ng]")
{
    for (int i = 0; i < 100; ++i)
    {
        auto n = gen().get_surname();
        REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
    }
}

TEST_CASE("get_name with specific gender", "[ng]")
{
    auto male = gen().get_name(dasmig::gender::m);
    REQUIRE_FALSE(static_cast<std::wstring>(male).empty());

    auto female = gen().get_name(dasmig::gender::f);
    REQUIRE_FALSE(static_cast<std::wstring>(female).empty());
}

TEST_CASE("get_name with specific culture", "[ng]")
{
    auto american = gen().get_name(dasmig::gender::any,
                                    dasmig::culture::american);
    REQUIRE_FALSE(static_cast<std::wstring>(american).empty());

    auto brazilian = gen().get_name(dasmig::gender::any,
                                     dasmig::culture::brazilian);
    REQUIRE_FALSE(static_cast<std::wstring>(brazilian).empty());
}

TEST_CASE("get_name with gender and culture", "[ng]")
{
    auto n = gen().get_name(dasmig::gender::f,
                             dasmig::culture::french);
    REQUIRE_FALSE(static_cast<std::wstring>(n).empty());
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
TEST_CASE("to_culture maps known codes", "[ng][utility]")
{
    REQUIRE(dasmig::ng::to_culture(L"us") == dasmig::culture::american);
    REQUIRE(dasmig::ng::to_culture(L"br") == dasmig::culture::brazilian);
    REQUIRE(dasmig::ng::to_culture(L"gb") == dasmig::culture::british);
    REQUIRE(dasmig::ng::to_culture(L"de") == dasmig::culture::german);
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

    auto a = static_cast<std::wstring>(
        g.get_name(dasmig::gender::m, dasmig::culture::american, seed));
    auto b = static_cast<std::wstring>(
        g.get_name(dasmig::gender::m, dasmig::culture::american, seed));
    REQUIRE(a == b);
}

TEST_CASE("seeded get_surname is deterministic", "[ng][seeding]")
{
    auto& g = gen();
    constexpr std::uint64_t seed = 99;

    auto a = static_cast<std::wstring>(
        g.get_surname(dasmig::culture::brazilian, seed));
    auto b = static_cast<std::wstring>(
        g.get_surname(dasmig::culture::brazilian, seed));
    REQUIRE(a == b);
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

TEST_CASE("get_surname on empty instance throws", "[ng][error]")
{
    dasmig::ng empty;
    REQUIRE_THROWS_AS(empty.get_surname(dasmig::culture::american),
                      std::invalid_argument);
}

// ===========================================================================
// random culture coverage (statistical)
// ===========================================================================
TEST_CASE("random culture covers multiple cultures", "[ng][random]")
{
    auto& g = gen();
    std::map<std::wstring, int> seen;

    for (int i = 0; i < 500; ++i)
    {
        auto n = static_cast<std::wstring>(g.get_name());
        seen[n]++; // just accumulate variety
    }

    // With 500 random names across 23 cultures, we should see variety.
    REQUIRE(seen.size() > 10);
}
