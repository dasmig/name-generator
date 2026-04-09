# Usage Guide

This guide covers every feature of the name-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a>
  <ul>
    <li><a href="#dataset-tiers">Dataset Tiers</a></li>
    <li><a href="#custom-paths">Custom Paths</a></li>
  </ul>
</li>
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
2. Copy the `resources/` folder (containing `.names` files) to your working directory. Choose `resources/lite/` (~2 MB) for compact datasets or `resources/full/` (~39 MB) for complete name lists.
3. Enable C++23: `-std=c++23` (GCC/Clang) or `/std:c++latest` (MSVC).

## Loading Resources

The singleton `ng::instance()` auto-probes `resources/`, `../resources/`, and `name-generator/resources/` on first access, preferring the `lite/` subfolder, then `full/`, then the flat root.

### Dataset Tiers

Two dataset tiers are available:

| Tier | Size | Content |
|------|------|---------|
| `dataset::lite` | ~2 MB | Top-500 names per category |
| `dataset::full` | ~39 MB | Complete name lists |

Load a specific tier:

```cpp
dasmig::ng gen;
gen.load(dasmig::dataset::lite);  // compact dataset
// or
gen.load(dasmig::dataset::full);  // full dataset
```

`load(dataset)` probes the same base paths as the singleton and returns `true` if a matching directory was found.

### Custom Paths

For custom locations:

```cpp
dasmig::ng::instance().load("/custom/path/to/resources");
```

> **Note:** `load()` silently does nothing if the path does not exist or is not a directory. Always use `has_resources()` to verify that databases were actually loaded.

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

105 cultures are supported:

| Culture | Code | Culture | Code | Culture | Code |
|---------|------|---------|------|---------|------|
| Afghan | `af` | Filipino | `ph` | Moldovan | `md` |
| Albanian | `al` | Finnish | `fi` | Moroccan | `ma` |
| Algerian | `dz` | French | `fr` | Namibian | `na` |
| American | `us` | Georgian | `ge` | Nigerian | `ng` |
| Angolan | `ao` | German | `de` | Norwegian | `no` |
| Argentinian | `ar` | Ghanaian | `gh` | Omani | `om` |
| Austrian | `at` | Greek | `gr` | Palestinian | `ps` |
| Azerbaijani | `az` | Guatemalan | `gt` | Panamanian | `pa` |
| Bahraini | `bh` | Haitian | `ht` | Peruvian | `pe` |
| Bangladeshi | `bd` | Honduran | `hn` | Polish | `pl` |
| Belgian | `be` | Hong Konger | `hk` | Portuguese | `pt` |
| Bolivian | `bo` | Hungarian | `hu` | Puerto Rican | `pr` |
| Botswanan | `bw` | Icelandic | `is` | Qatari | `qa` |
| Brazilian | `br` | Indian | `in` | Russian | `ru` |
| British | `gb` | Indonesian | `id` | Salvadoran | `sv` |
| Bruneian | `bn` | Iranian | `ir` | Saudi | `sa` |
| Bulgarian | `bg` | Iraqi | `iq` | Serbian | `rs` |
| Burkinabe | `bf` | Irish | `ie` | Singaporean | `sg` |
| Burundian | `bi` | Israeli | `il` | Slovenian | `si` |
| Cambodian | `kh` | Italian | `it` | South African | `za` |
| Cameroonian | `cm` | Jamaican | `jm` | Spanish | `es` |
| Canadian | `ca` | Japanese | `jp` | Sudanese | `sd` |
| Chilean | `cl` | Jordanian | `jo` | Swedish | `se` |
| Chinese | `cn` | Kazakh | `kz` | Swiss | `ch` |
| Colombian | `co` | Korean | `kr` | Syrian | `sy` |
| Costa Rican | `cr` | Kuwaiti | `kw` | Taiwanese | `tw` |
| Croatian | `hr` | Lebanese | `lb` | Tunisian | `tn` |
| Cypriot | `cy` | Libyan | `ly` | Turkish | `tr` |
| Czech | `cz` | Lithuanian | `lt` | Turkmen | `tm` |
| Danish | `dk` | Luxembourgish | `lu` | Uruguayan | `uy` |
| Djiboutian | `dj` | Macanese | `mo` | Yemeni | `ye` |
| Dutch | `nl` | Malaysian | `my` | | |
| Ecuadorian | `ec` | Maldivian | `mv` | | |
| Egyptian | `eg` | Maltese | `mt` | | |
| Emirati | `ae` | Mauritian | `mu` | | |
| Estonian | `ee` | Mexican | `mx` | | |
| Ethiopian | `et` | | | | |
| Fijian | `fj` | | | | |

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

### Lifetime Safety

A `name` object holds a back-pointer to the `ng` instance that created it.
Calling `append_name()` or `append_surname()` on a `name` whose `ng` instance
has been destroyed is **undefined behavior**. Keep the generator alive for as
long as you need to chain appends:

```cpp
dasmig::ng gen;
gen.load("resources");

auto n = gen.get_name();
// gen must remain alive while we append:
n.append_surname();

// After this point the name is self-contained — gen can be destroyed.
std::wstring result = n;
```

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

> **Note:** `load()` does **not** throw if the path is missing — it silently returns. Use `has_resources()` to check.
