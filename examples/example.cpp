#include "../dasmig/namegen.hpp"
#include <cstdint>
#include <iostream>
#include <string>

namespace
{

// Names are std::wstring; print them as UTF-8, which std::cout passes
// through unchanged (std::wcout stops at the first non-ASCII letter unless a
// locale is set).
std::string utf8(const dasmig::name& n)
{
    return dasmig::core::to_utf8(std::wstring{n});
}

} // namespace

int main() // NOLINT(bugprone-exception-escape)
{
    auto& gen = dasmig::ng::instance();

    // Random name + surname (any gender, any culture).
    std::cout << "Random:     " << utf8(gen.get_name({}).append_surname()) << "\n";

    // French female name + surname.
    std::cout << "French (F): "
              << utf8(gen.get_name({.gender = dasmig::gender::f, .culture = dasmig::culture::french}).append_surname())
              << "\n";

    // American male name + middle name + surname.
    std::cout << "American:   "
              << utf8(              gen.get_name({.gender = dasmig::gender::m, .culture = dasmig::culture::american})
                          .append_name()
                          .append_surname())
              << "\n";

    // Deterministic generation: the same seed gives the same name on every
    // compiler and platform.
    constexpr std::uint64_t seed = 42;
    auto seeded = gen.get_name({.gender = dasmig::gender::m, .culture = dasmig::culture::german}, seed);
    std::cout << "Seeded:     " << utf8(seeded) << "  (seed=" << seeded.seed() << ")\n";

    // Appended parts replay too: they draw from the name's seed.
    auto full = gen.get_name({.gender = dasmig::gender::f, .culture = dasmig::culture::italian}, seed);
    full.append_surname();
    std::cout << "JSON:       " << full.to_json() << "\n";

    // Add names of your own on top of the shipped lists (.names format: the
    // country code, the gender, then one name per line with a weight).
    gen.extend_from_string("br\nfemale\nIracema\t0.5\nJaci\t0.5\n", "example.names");
    std::cout << "Extended:   "
              << utf8(gen.get_name({.gender = dasmig::gender::f, .culture = dasmig::culture::brazilian})) << "\n";

    // Independent instance with its own databases.
    dasmig::ng my_gen;
    (void)my_gen.load(dasmig::dataset::lite); // or dataset::full for all names
    std::cout << "Instance:   " << utf8(my_gen.get_name({}, seed).append_surname()) << "\n";

    return 0;
}
