# Usage Guide

This guide covers every feature of the name-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-names">Generating Names</a></li>
<li><a href="#cultures-and-genders">Cultures and Genders</a></li>
<li><a href="#chaining-names-and-surnames">Chaining Names and Surnames</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#multi-instance-support">Multi-Instance Support</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/namegen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::ng::instance();

    // Random name with a surname.
    std::wcout << gen.get_name().append_surname() << L"\n";

    // American female name with surname.
    std::wcout << gen.get_name(dasmig::gender::f, dasmig::culture::american)
                      .append_surname()
               << L"\n";
}
```

## Installation

1. Copy `dasmig/namegen.hpp` and `dasmig/random.hpp` into your include path.
2. Copy the `resources/` folder (containing `.names` files) to your working directory.
3. Enable C++23: `-std=c++23` (GCC/Clang) or `/std:c++latest` (MSVC).

## Loading Resources

The singleton `ng::instance()` auto-probes `resources/`, `../resources/`, and `name-generator/resources/` on first access. For custom locations:

```cpp
dasmig::ng::instance().load("/custom/path/to/resources");
```

Check whether resources loaded successfully:

```cpp
if (!dasmig::ng::instance().has_resources())
{
    dasmig::ng::instance().load("/path/to/resources");
}
```

## Generating Names

```cpp
auto& gen = dasmig::ng::instance();

// Random first name (any gender, any culture).
auto first = gen.get_name();

// Random surname (any culture).
auto last = gen.get_surname();
```

## Cultures and Genders

23 cultures are supported:

| Culture | Code | Culture | Code |
|---------|------|---------|------|
| American | `us` | Mexican | `mx` |
| Argentinian | `ar` | Norwegian | `no` |
| Australian | `au` | Polish | `pl` |
| Brazilian | `br` | Portuguese | `pt` |
| British | `gb` | Russian | `ru` |
| Bulgarian | `bg` | Spanish | `es` |
| Canadian | `ca` | Swedish | `se` |
| Chinese | `cn` | Turkish | `tr` |
| Danish | `dk` | Ukrainian | `ua` |
| Finnish | `fi` | | |
| French | `fr` | | |
| German | `de` | | |
| Kazakh | `kz` | | |

```cpp
// Specific culture and gender.
auto n = gen.get_name(dasmig::gender::m, dasmig::culture::german);

// Convert from ISO 3166 code.
auto culture = dasmig::ng::to_culture(L"br"); // culture::brazilian
auto gender = dasmig::ng::to_gender(L"female"); // gender::f
```

## Chaining Names and Surnames

The `name` return type supports chained appending:

```cpp
// First name + middle name + surname.
auto full = gen.get_name().append_name().append_surname();
std::wcout << full << L"\n";

// Access individual parts.
for (const auto& part : full.parts())
{
    std::wcout << part << L" ";
}

// Append with a different culture.
auto mixed = gen.get_name(dasmig::gender::f, dasmig::culture::chinese)
                 .append_surname(dasmig::culture::american);
```

## Thread Safety

Each `ng` instance is independent. The static `instance()` singleton uses a
local static for safe initialization.

| Operation | Thread-safe? |
|-----------|-------------|
| `instance()` | Yes (static local) |
| `get_name()` / `get_surname()` on **different** instances | Yes |
| `get_name()` / `get_surname()` on the **same** instance | **No** — requires external synchronization |
| `load()` | **No** — must not be called concurrently with generation on the same instance |

For concurrent generation, give each thread its own `ng` instance.

## Seeding and Deterministic Generation

### Per-call seeding

Pass an explicit seed to produce the same name every time:

```cpp
auto n = gen.get_name(dasmig::gender::m, dasmig::culture::american, 42);
// Always produces the same result for the same parameters + seed.
```

### Seed replay

Every name records its seed. Retrieve it with `name::seed()`:

```cpp
auto original = gen.get_name();
auto replay = gen.get_name(dasmig::gender::any, dasmig::culture::any,
                            original.seed());
```

### Sequence seeding

Seed the generator engine for deterministic sequences:

```cpp
gen.seed(100);
auto a = gen.get_name(); // deterministic
auto b = gen.get_name(); // deterministic
gen.unseed();             // restore non-deterministic state
```

## Multi-Instance Support

Construct independent `ng` objects with their own name databases and random engine:

```cpp
dasmig::ng my_gen;
my_gen.load("path/to/resources");
std::wcout << my_gen.get_name().append_surname() << L"\n";
```

### Separate Seeding

Each instance maintains its own engine:

```cpp
dasmig::ng a;
dasmig::ng b;
a.load("resources");
b.load("resources");

a.seed(42);
b.seed(99);
// a and b produce different sequences.
```

### Move Semantics

Instances can be moved (but not copied):

```cpp
dasmig::ng src;
src.load("resources");
dasmig::ng dst = std::move(src);
```

## Error Reference

| Exception | Thrown by | Condition |
|-----------|-----------|----------|
| `std::invalid_argument` | `get_name()` / `get_surname()` | No names loaded for the resolved culture/gender |
