// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_BENCH_HPP
#define DASMIG_BENCH_HPP

/// @file bench.hpp
/// @brief Benchmark harness shared by the dasmig libraries.
///
/// The file is identical in every library (tools/check-conformance.sh checks
/// it). A library's bench/bench.cpp times loading and its getters:
///
/// @code
/// dasmig::bench::runner run{"biodata-generator", argc, argv};
/// run.load("load", [&] { dasmig::bdg g; return g.load(run.tier()); });
/// dasmig::bdg gen;
/// if (!gen.load(run.tier())) return run.missing_tier();
/// run.calls("get_biodata", [&](std::uint64_t s) { return gen.get_biodata({}, s); });
/// return run.finish();
/// @endcode
///
/// Options: `--tier lite|full` (default lite), `--out FILE` (append the
/// results as TSV), `--calls N` (default 10000), `--loads N` (default 5),
/// `--quick` (1000 calls, 1 load), `--gate` (a fixed workload with no timing
/// loops, for scripts/gate.py to count instructions under cachegrind).
///
/// What is measured:
/// - a load: median and minimum wall time over `--loads` fresh instances;
/// - a getter: the mean from an untimed loop over `--calls` fixed seeds, then
///   the median and 99th percentile with each call timed (these include the
///   clock's own cost, tens of nanoseconds);
/// - a batch: items per second at 1, 4 and 8 threads (median of three runs);
/// - the process's peak memory at the end (resident set natively, linear
///   memory in WebAssembly).

#include "../dasmig/core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace dasmig::bench
{

/// @brief Keeps a value alive so the optimizer cannot remove the call that
///        produced it.
template <class T> inline void keep(const T& value)
{
#if defined(_MSC_VER) && !defined(__clang__)
    static const void* volatile sink{};
    sink = &value;
    _ReadWriteBarrier();
#else
    asm volatile("" : : "r"(&value) : "memory");
#endif
}

/// @brief The i-th benchmark seed (a fixed sequence, the same everywhere).
[[nodiscard]] constexpr std::uint64_t seed(std::size_t index) noexcept
{
    return core::rng{0x5EEDBE7C}.child_seed(static_cast<std::uint64_t>(index));
}

/// @brief Compiler, standard library and CPU of this build.
[[nodiscard]] inline std::string build_info()
{
    std::string info;
#if defined(__EMSCRIPTEN__)
    info = "emscripten " + std::to_string(__EMSCRIPTEN_major__) + "." +
           std::to_string(__EMSCRIPTEN_minor__) + "." + std::to_string(__EMSCRIPTEN_tiny__);
#elif defined(__clang__)
    info = "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    info = "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
    info = "msvc " + std::to_string(_MSC_VER);
#endif
#if defined(_LIBCPP_VERSION)
    info += ", libc++";
#elif defined(__GLIBCXX__)
    info += ", libstdc++";
#elif defined(_MSVC_STL_VERSION)
    info += ", msvc-stl";
#endif
#if defined(__wasm__)
    info += ", wasm32";
#elif defined(__x86_64__) || defined(_M_X64)
    info += ", x86-64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    info += ", arm64";
#endif
    return info;
}

/// @brief Peak memory of the process in bytes (0 when unknown).
[[nodiscard]] inline std::uint64_t peak_memory()
{
#if defined(__EMSCRIPTEN__)
    return static_cast<std::uint64_t>(__builtin_wasm_memory_size(0)) * 65536U;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters) == 0) return 0;
    return counters.PeakWorkingSetSize;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#endif
}

/// @brief Runs a library's benchmarks and reports them.
class runner
{
  public:
    /// @brief Reads the options from the command line.
    runner(std::string library, int argc, char** argv) : _library{std::move(library)}
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg{argv[i]};
            const auto value = [&]() -> std::string_view {
                if (i + 1 >= argc) fail("missing value after " + std::string{arg});
                return argv[++i];
            };
            if (arg == "--tier")
            {
                const auto tier = value();
                if (tier == "lite") _tier = dataset::lite;
                else if (tier == "full") _tier = dataset::full;
                else fail("--tier must be lite or full");
            }
            else if (arg == "--out") _out = value();
            else if (arg == "--calls") _calls = count(value());
            else if (arg == "--loads") _loads = count(value());
            else if (arg == "--quick") { _calls = 1000; _loads = 1; }
            else if (arg == "--gate") { _calls = 1000; _loads = 1; _gate = true; }
            else fail("unknown option " + std::string{arg});
        }
#if defined(DASMIG_BENCH_EMBEDDED)
        _source = _tier == dataset::lite ? "embedded" : "disk";
#else
        _source = "disk";
#endif
        std::printf("%s  tier %s (%s)  %s\n", _library.c_str(), tier_name(), _source.c_str(),
                    build_info().c_str());
    }

    /// @brief The tier to benchmark.
    [[nodiscard]] dataset tier() const noexcept { return _tier; }

    /// @brief Times `load()` (which builds a fresh instance and returns
    ///        whether the tier was found) over several runs.
    template <class Load> void load(std::string_view name, Load&& load)
    {
        std::vector<double> ms;
        for (std::size_t i = 0; i < _loads; ++i)
        {
            const auto start = clock::now();
            const bool found = load();
            ms.push_back(elapsed_ms(start));
            if (!found) return;
        }
        std::ranges::sort(ms);
        add(name, "load_median", ms[ms.size() / 2], "ms");
        add(name, "load_min", ms.front(), "ms");
    }

    /// @brief Times `call(seed)` over the fixed seeds.
    template <class Call> void calls(std::string_view name, Call&& call)
    {
        if (_gate)
        {
            for (std::size_t i = 0; i < _calls; ++i) keep(call(seed(i)));
            return;
        }
        const std::size_t warmup = std::min<std::size_t>(_calls, 200);
        for (std::size_t i = 0; i < warmup; ++i) keep(call(seed(i)));

        const auto start = clock::now();
        for (std::size_t i = 0; i < _calls; ++i) keep(call(seed(i)));
        const double total_us = elapsed_ms(start) * 1000.0;

        std::vector<double> us(_calls);
        for (std::size_t i = 0; i < _calls; ++i)
        {
            const auto one = clock::now();
            keep(call(seed(i)));
            us[i] = elapsed_ms(one) * 1000.0;
        }
        std::ranges::sort(us);
        const double mean = total_us / static_cast<double>(_calls);
        add(name, "mean", mean, "us");
        add(name, "p50", us[us.size() / 2], "us");
        add(name, "p99", us[std::min(us.size() - 1, us.size() * 99 / 100)], "us");
        add(name, "per_second", 1e6 / mean, "calls");
    }

    /// @brief Times a batch call at 1, 4 and 8 threads and reports items per
    ///        second. `batch(count, threads, seed)` returns the items.
    template <class Batch> void batches(std::string_view name, Batch&& batch)
    {
        if (_gate)
        {
            keep(batch(std::size_t{1000}, std::size_t{1}, seed(0)));
            return;
        }
        const std::size_t items = std::max<std::size_t>(_calls, 1000);
        for (const std::size_t threads : {1U, 4U, 8U})
        {
            keep(batch(std::min<std::size_t>(items, 200), threads, seed(0))); // warm up
            std::vector<double> ms;
            for (std::size_t run = 0; run < 3; ++run)
            {
                const auto start = clock::now();
                keep(batch(items, threads, seed(run + 1)));
                ms.push_back(elapsed_ms(start));
            }
            std::ranges::sort(ms);
            const std::string label = std::string{name} + " x" + std::to_string(items) + ", " +
                                      std::to_string(threads) + (threads == 1 ? " thread" : " threads");
            add(label, "per_second", static_cast<double>(items) / (ms[1] / 1000.0), "items");
        }
    }

    /// @brief Reports a tier this library does not ship, and ends.
    [[nodiscard]] int missing_tier() const
    {
        std::printf("  tier %s not found; skipped\n", tier_name());
        return 0;
    }

    /// @brief Adds the peak memory, prints the results and appends them to
    ///        `--out`.
    [[nodiscard]] int finish()
    {
        add("process", "peak_memory", static_cast<double>(peak_memory()) / (1024.0 * 1024.0), "MiB");
        if (_out.empty()) return 0;
        bool header = false;
        {
            std::ifstream existing{_out};
            header = !existing || existing.peek() == std::ifstream::traits_type::eof();
        }
        std::ofstream out{_out, std::ios::app};
        if (!out) fail("cannot write " + _out);
        if (header) out << "library\ttier\tsource\tbenchmark\tmetric\tvalue\tunit\tbuild\n";
        const auto build = build_info();
        for (const auto& r : _results)
        {
            out << _library << '\t' << tier_name() << '\t' << _source << '\t' << r.benchmark << '\t'
                << r.metric << '\t' << r.value << '\t' << r.unit << '\t' << build << '\n';
        }
        return 0;
    }

  private:
    using clock = std::chrono::steady_clock;

    struct result
    {
        std::string benchmark;
        std::string metric;
        double value;
        std::string unit;
    };

    [[noreturn]] static void fail(const std::string& message)
    {
        std::fprintf(stderr, "bench: %s\n", message.c_str());
        std::exit(2);
    }

    static std::size_t count(std::string_view text)
    {
        std::size_t n = 0;
        for (const char c : text)
        {
            if (c < '0' || c > '9') fail("not a count: " + std::string{text});
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        if (n == 0) fail("a count must be positive");
        return n;
    }

    static double elapsed_ms(clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(clock::now() - start).count();
    }

    [[nodiscard]] const char* tier_name() const { return _tier == dataset::lite ? "lite" : "full"; }

    void add(std::string_view benchmark, std::string_view metric, double value, std::string_view unit)
    {
        std::printf("  %-34s %-12s %12.3f %s\n", std::string{benchmark}.c_str(),
                    std::string{metric}.c_str(), value, std::string{unit}.c_str());
        _results.push_back({std::string{benchmark}, std::string{metric}, value, std::string{unit}});
    }

    std::string _library;
    dataset _tier{dataset::lite};
    bool _gate{false};
    std::string _source;
    std::string _out;
    std::size_t _calls{10000};
    std::size_t _loads{5};
    std::vector<result> _results;
};

} // namespace dasmig::bench

#endif // DASMIG_BENCH_HPP
