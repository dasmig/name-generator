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
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#migrating-from-v2">Migrating from v2</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/namegen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::ng::instance();

    // Random name with a surname, printed as UTF-8 (std::wcout stops at the
    // first non-ASCII letter unless a locale is set).
    std::wstring name = gen.get_name().append_surname();
    std::cout << dasmig::core::to_utf8(name) << "\n";

    // American female name with surname.
    name = gen.get_name(dasmig::gender::f, dasmig::culture::american).append_surname();
    std::cout << dasmig::core::to_utf8(name) << "\n";
}
```

## Installation

1. Copy `dasmig/namegen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them side by side.
2. Copy the `resources/` folder (containing `.names` files) to your working directory. Choose `resources/lite/` (3 MB) for compact datasets or `resources/full/` (41 MB) for complete name lists. Or compile `lite` in (see Compiled-in Data).
3. Enable C++23: `-std=c++23` (GCC/Clang) or `/std:c++latest` (MSVC).

## Loading Resources

The singleton `ng::instance()` loads the `lite` tier on first access, or `full` when `lite` is not found. It uses the compiled-in copy when built with `DASMIG_NAME_EMBED`, and otherwise looks in `resources`, `../resources`, `name-generator/resources` and `../name-generator/resources`, relative to the working directory.

### Dataset Tiers

Two dataset tiers are available:

| Tier | Size | Content |
|------|------|---------|
| `dataset::lite` | 3 MB | Up to 500 names per list (133,009 names) |
| `dataset::full` | 41 MB | Complete name lists (2,273,752 names) |

Where the names come from, how they are weighted and what they leave out is in [Data and Method](data.md). The full tier takes about 2 s to load (optimised build), the lite tier about 0.15 s.

Load a specific tier:

```cpp
dasmig::ng gen;
(void)gen.load(dasmig::dataset::lite);  // compact dataset
// or
(void)gen.load(dasmig::dataset::full);  // full dataset
```

`load(dataset)` probes the same base paths as the singleton and returns `true` if the tier was found. Loading replaces the names and any layers loaded before.

### Custom Paths

For custom locations:

```cpp
dasmig::ng::instance().load("/custom/path/to/resources");
```

`load(path)` reads every `.names` file below the directory, in path order, and replaces what was loaded. It does nothing if the path does not exist or is not a directory, so use `has_resources()` to verify that databases were actually loaded. A malformed file throws `dasmig::core::tsv_error` with its file, line and column, and leaves the names unchanged.

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

// Convert from ISO 3166 code, and back.
auto culture = dasmig::ng::to_culture(L"br"); // culture::brazilian
auto gender = dasmig::ng::to_gender(L"female"); // gender::f
dasmig::ng::culture_code(culture);  // "br"
dasmig::ng::culture_name(culture);  // "brazilian"
```

With `culture::any` a culture is chosen uniformly among the loaded ones, not weighted by population; to follow a population, choose the country first (for example with country-generator) and pass its culture.

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

Each appended part draws from the name's seed and its position, so replaying the seed and the same appends gives the same full name (see Replay and JSON).

## Thread Safety

Each `ng` instance is independent. The static `instance()` singleton uses a
local static for safe initialization.

| Operation | Thread-safe? |
|-----------|-------------|
| `instance()` | Yes (static local) |
| `get_name()` / `get_surname()` on **different** instances | Yes |
| `get_name()` / `get_surname()` on the **same** instance | **No** — requires external synchronization |
| `load()`, `extend()` | **No** — must not be called concurrently with generation on the same instance |

For concurrent generation, give each thread its own `ng` instance. The seeded overloads (`get_name(g, c, seed)`, `get_surname(c, seed)`) are `const` and do not touch the engine.

### Lifetime Safety

A `name` object holds a back-pointer to the `ng` instance that created it.
Calling `append_name()` or `append_surname()` on a `name` whose `ng` instance
has been destroyed or moved from is **undefined behavior**. Keep the generator alive for as
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
// The same parameters, seed and data give the same name on every compiler,
// standard library and CPU.
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

## Extending Data

The shipped lists are a base layer. Files in the same `.names` format are loaded on top of them to add names, change their weights or remove them, without editing the shipped files. Each file says which list it changes on its first two lines (country code, then `male`, `female` or `any` for surnames, or `male-surname` or `female-surname` for the cultures with gendered surnames, see [data.md](data.md#gendered-surnames)); within a list, names are the keys.

```cpp
auto& gen = dasmig::ng::instance();

// Override mode (the default): a name already in the list gets the layer's
// weight; new names are added.
gen.extend("mods/brazilian_male.names");

// A directory: every .names file below it, applied together.
gen.extend("mods/");

// From text, for example supplied by a WebAssembly host or a game scenario.
gen.extend_from_string("br\nfemale\nIracema\t0.5\nJaci\t0.5\n", "scenario: coast");

// Remove names with a third column, disabled; a list left empty is unloaded.
gen.extend_from_string("br\nmale\nLucas\t\t1\n", "cleanup");

// Add mode refuses names that are already in the list.
gen.extend("mods/new_names.names", dasmig::core::layer_mode::add);
```

- Weights are relative within a list: the shipped weights are `1 / sqrt(rank)` (the top name about 0.7 to 1.0, rank 500 about 0.045), and a name without a weight counts 1, so it is about as likely as the most popular name.
- A layer is checked before anything changes: the country code and list line, UTF-8, positive weights, no name twice. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the names stay as they were; a directory is applied all or nothing.
- `layers(culture, gender)` lists what is loaded for one list (`gender::any` for surnames): source, mode, names added, replaced and removed, and warnings.
- `load(...)` replaces the base and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto n = gen.get_name(dasmig::gender::f, dasmig::culture::italian);
n.append_surname();
n.seed();                    // the call seed
n.signature().data;          // gen.data_hash() when it was generated

// Same seed, same appends, same data: the same full name everywhere.
auto again = gen.get_name(dasmig::gender::f, dasmig::culture::italian, n.seed());
again.append_surname();

if (n.signature().data != gen.data_hash())
{
    // Different layers are loaded now; the replay would differ.
}
```

`to_json()` gives the result as JSON (UTF-8), the form the WebAssembly layer and the game consume:

```json
{"name":"Katia Russo","parts":["Katia","Russo"],"gender":"female","culture":"italian",
 "signature":{"seed":"000000000000002a","data":"d7aadf4cfcb4c207"}}
```

`gender` is the gender of the forenames (for a surname, the one `append_name()` would use) and `culture` the culture of the first part. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_NAME_EMBED` before including the header to compile the `lite` tier into the program (a 4 MB header; about 5 s to compile with clang). `load(dataset::lite)` and `instance()` then use it and need no files, which is what a WebAssembly build wants. The compiled-in data has the same `data_hash()` as the files, so seeds replay the same either way. The `full` tier is always read from disk.

```cpp
#define DASMIG_NAME_EMBED
#include <dasmig/namegen.hpp>
```

`dasmig/namegen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Migrating from v2

Version 3 moves the library onto [dasmig-core](https://github.com/dasmig/dasmig-core).

- **Every generated name changes.** The engine (xoshiro256\*\*) and the weighted choice are now the core's own, so a seed gives the same name on every compiler, standard library and CPU; `std::discrete_distribution` gave different names with libstdc++, libc++ and MSVC. Seeds saved with v2 do not reproduce their v2 results. The distributions are unchanged (checked against v2 over 20,000 draws per scenario).
- **Full 64-bit seeds.** v2 folded the seed to 32 bits for its engine, so different seeds could give the same name.
- **Independent draws.** Gender, culture and name each draw from their own stream, so forcing the gender a random draw would have picked gives the same name.
- **Appended parts replay.** v2 drew them from the generator's engine; v3 draws them from the name's seed and the part's position, and appending no longer advances the engine.
- **`load(path)` replaces** the names instead of adding to them; use `extend()` to add. A malformed file now throws `dasmig::core::tsv_error` (v2 skipped files with an unknown country code, read any second line other than `male`/`female` as surnames, and dropped invalid UTF-8 bytes). `load(dataset)` is `[[nodiscard]]`.
- **Data.** `resources/full` lost two folders v2 never loaded (`australian`, `ukranian`). The lists are otherwise unchanged.
- **Files:** replace `random.hpp` with `core.hpp` and the `core/` directory (see Installation).
- **New:** `extend()`, `extend_from_string()`, `layers()`, `data_hash()`, `culture_code()`, `culture_name()`, `name::signature()`, `name::to_json()`, `DASMIG_NAME_EMBED`, a `disabled` column.

## Error Reference

| Exception | Thrown by | Condition |
|-----------|-----------|----------|
| `std::invalid_argument` | `get_name()` / `get_surname()` / `append_*()` | No names loaded for the resolved culture/gender |
| `std::invalid_argument` | `extend(path)` | A file without the `.names` extension |
| `std::out_of_range` | `layers(culture, gender)` | That list was never loaded |
| `dasmig::core::tsv_error` | `load()`, `extend()`, `extend_from_string()` | A malformed file or layer (message gives file, line and column); the names are unchanged |

> **Note:** `load(path)` does **not** throw if the path is missing — it silently returns. Use `has_resources()` to check.
