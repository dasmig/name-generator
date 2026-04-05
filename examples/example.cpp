#include "../dasmig/namegen.hpp"
#include <cstdint>
#include <iostream>

int main() // NOLINT(bugprone-exception-escape)
{
    auto& gen = dasmig::ng::instance();

    // Random name + surname (any gender, any culture).
    std::wcout << L"Random:     " << gen.get_name().append_surname() << L"\n";

    // French female name + surname.
    std::wcout << L"French (F): "
               << gen.get_name(dasmig::gender::f, dasmig::culture::french)
                      .append_surname()
               << L"\n";

    // American male name + middle name + surname.
    std::wcout << L"American:   "
               << gen.get_name(dasmig::gender::m, dasmig::culture::american)
                      .append_name()
                      .append_surname()
               << L"\n";

    // Deterministic generation — same seed always gives the same name.
    constexpr std::uint64_t seed = 42;
    auto seeded = gen.get_name(dasmig::gender::m, dasmig::culture::german, seed);
    std::wcout << L"Seeded:     " << seeded << L"  (seed=" << seeded.seed()
               << L")\n";

    // Independent instance with its own databases and engine.
    constexpr std::uint64_t instance_seed = 100;
    dasmig::ng my_gen;
    my_gen.load("resources");
    my_gen.seed(instance_seed);
    std::wcout << L"Instance:   " << my_gen.get_name().append_surname()
               << L"\n";

    return 0;
}