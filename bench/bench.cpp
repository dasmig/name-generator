// Benchmarks for name-generator; see bench.hpp for the options.

#include "../dasmig/namegen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"name-generator", argc, argv};

    run.load("load", [&] {
        dasmig::ng gen;
        return gen.load(run.tier());
    });

    dasmig::ng gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_name", [&](std::uint64_t seed) { return gen.get_name({}, seed); });
    run.calls("get_name brazilian female", [&](std::uint64_t seed) {
        return gen.get_name({.gender = dasmig::gender::f, .culture = dasmig::culture::brazilian}, seed);
    });
    run.calls("get_surname", [&](std::uint64_t seed) { return gen.get_surname({}, seed); });
    run.calls("get_name to_json", [&](std::uint64_t seed) {
        return gen.get_name({}, seed).to_json();
    });

    return run.finish();
}
