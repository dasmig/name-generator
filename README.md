# Name Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Name Generator for C++](https://raw.githubusercontent.com/dasmig/name-generator/master/doc/name-generator.png)](https://github.com/dasmig/name-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/name-generator/master/LICENSE.MIT)
[![CI](https://github.com/dasmig/name-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/name-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/name-generator.svg)](https://github.com/dasmig/name-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/name-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/name-generator/)

**[API Reference](https://dasmig.github.io/name-generator/)** · **[Usage Guide](doc/usage.md)** · **[Releases](https://github.com/dasmig/name-generator/releases)**

Culturally appropriate first names and surnames from popular name databases, by culture and gender.

## Features

- **Name Generation**. Randomly generates first names from popular name databases, organized by gender and culture.

- **Surname Generation**. Randomly generates surnames, organized by culture.

- **Full Name Chaining**. Append one or more names and surnames with an intuitive chainable API.

- **105 Cultures Supported**. Afghan, Albanian, Algerian, American, Angolan, Argentinian, Austrian, Azerbaijani, Bahraini, Bangladeshi, Belgian, Bolivian, Botswanan, Brazilian, British, Bruneian, Bulgarian, Burkinabe, Burundian, Cambodian, Cameroonian, Canadian, Chilean, Chinese, Colombian, Costa Rican, Croatian, Cypriot, Czech, Danish, Djiboutian, Dutch, Ecuadorian, Egyptian, Emirati, Estonian, Ethiopian, Fijian, Filipino, Finnish, French, Georgian, German, Ghanaian, Greek, Guatemalan, Haitian, Honduran, Hong Konger, Hungarian, Icelandic, Indian, Indonesian, Iranian, Iraqi, Irish, Israeli, Italian, Jamaican, Japanese, Jordanian, Kazakh, Korean, Kuwaiti, Lebanese, Libyan, Lithuanian, Luxembourgish, Macanese, Malaysian, Maldivian, Maltese, Mauritian, Mexican, Moldovan, Moroccan, Namibian, Nigerian, Norwegian, Omani, Palestinian, Panamanian, Peruvian, Polish, Portuguese, Puerto Rican, Qatari, Russian, Salvadoran, Saudi, Serbian, Singaporean, Slovenian, South African, Spanish, Sudanese, Swedish, Swiss, Syrian, Taiwanese, Tunisian, Turkish, Turkmen, Uruguayan, Yemeni.

- **Weighted Selection**. Names are weighted by their popularity rank in each country so that common names appear more often than rare ones (see [Data and Method](doc/data.md)).

- **Dataset Tiers**. Choose between a compact **lite** dataset (3 MB, up to 500 names per list) or the **full** dataset (41 MB, complete name lists).

- **ISO 2-Letter Codes**. `to_culture()` and `to_gender()` for converting strings to enums.

- **Resource Inspection**. `has_resources()` lets you verify databases loaded before generating.

- **Deterministic Seeding**. Per-call `get_name(name_request, seed)` (the request carries the gender and culture) for reproducible results, and `name::seed()` for replaying a previous generation, appended parts included.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same name on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own `.names` layers on top of the shipped lists to add, reweight or remove names (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_NAME_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `ng` instances with their own name databases.

## Integration

[`namegen.hpp`](https://github.com/dasmig/name-generator/blob/master/dasmig/namegen.hpp) is the library [released here](https://github.com/dasmig/name-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/name-generator/blob/master/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/name-generator/tree/master/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/namegen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/name-generator/tree/master/resources) folder (or define `DASMIG_NAME_EMBED`).

Version 3 changes every seeded result; see [Migrating from v2](doc/usage.md#migrating-from-v2). Version 4 makes `name_request` the only way to call a getter; see [Migrating from v3](doc/usage.md#migrating-from-v3).

## Usage

Due to the necessity of supporting multiple culture characters and the way `std::string` works on Windows, this library uses `std::wstring` for all generated names; `dasmig::core::to_utf8()` converts them, and `to_json()` is UTF-8.

When requesting a name for the first time the singleton loads the **lite** tier, from the compiled-in copy or from `resources/lite` (it looks in `resources`, `../resources`, `name-generator/resources` and `../name-generator/resources`). Manually load the resources folder if it's in a different location, or use `load(dataset)` to pick between the **lite** and **full** tiers. Loading replaces what was loaded; `extend()` adds to it.

```cpp
#include <dasmig/namegen.hpp>

using ng = dasmig::ng;

// Load the lite dataset (3 MB, up to 500 names per list).
(void)ng::instance().load(dasmig::dataset::lite);

// Or load the full dataset (41 MB, complete name lists).
// (void)ng::instance().load(dasmig::dataset::full);

// Manually load a custom directory if necessary.
// ng::instance().load("path/to/resources");

// Generate a name of any culture and any gender.
std::wstring name = ng::instance().get_name({});

// Generate a female name of any culture.
std::wstring female_name = ng::instance().get_name({.gender = dasmig::gender::f});

// Generate a male Bulgarian name.
std::wstring bulgarian = ng::instance().get_name({.gender = dasmig::gender::m,
                                                  .culture = dasmig::culture::bulgarian});

// Generate a full name: first + two surnames.
std::wstring full = ng::instance().get_name({}).append_surname().append_surname();

// Access individual parts.
auto n = ng::instance().get_name({}).append_surname();
for (const auto& part : n.parts())
    std::wcout << part << L" ";

// Deterministic generation — same seed always produces the same name.
auto seeded = ng::instance().get_name({.gender = dasmig::gender::m,
                                       .culture = dasmig::culture::american}, 42);

// Replay a previous name using its seed.
auto replay = ng::instance().get_name({.gender = dasmig::gender::m,
                                       .culture = dasmig::culture::american},
                                       seeded.seed());

// The result as JSON, with the seed and the data it came from.
std::string json = seeded.append_surname().to_json();

// Add names of your own on top of the shipped lists.
ng::instance().extend("mods/brazilian_female.names");

// Independent instance with its own databases.
ng my_gen;
my_gen.load("path/to/resources");
std::wstring name2 = my_gen.get_name({}).append_surname();
```

For the complete feature guide — cultures, chaining, thread safety, seeding, extending data, compiled-in data, and more — see the **[Usage Guide](doc/usage.md)**. Where the names come from and what they leave out: **[Data and Method](doc/data.md)**.

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/namegen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.0 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [address-generator](https://github.com/dasmig/address-generator) | Postal addresses with real streets by place |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [email-generator](https://github.com/dasmig/email-generator) | Email addresses in each country's style |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [trait-generator](https://github.com/dasmig/trait-generator) | Personality traits with conflicts, synergies and Big Five axes |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2020-2026 Diego Dasso Migotto
```
