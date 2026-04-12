# Name Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Name Generator for C++](https://raw.githubusercontent.com/dasmig/name-generator/master/doc/name-generator.png)](https://github.com/dasmig/name-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/name-generator/master/LICENSE.MIT)
[![CI](https://github.com/dasmig/name-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/name-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/name-generator.svg)](https://github.com/dasmig/name-generator/releases)
[![GitHub Issues](https://img.shields.io/github/issues/dasmig/name-generator.svg)](https://github.com/dasmig/name-generator/issues)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/name-generator#integration)
[![Platform](https://img.shields.io/badge/platform-linux%20|%20windows%20|%20macos-lightgrey.svg)](https://github.com/dasmig/name-generator)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/name-generator/)

**[API Reference](https://dasmig.github.io/name-generator/)** · **[Usage Guide](doc/usage.md)** · **[Releases](https://github.com/dasmig/name-generator/releases)**

## Features

- **Name Generation**. Randomly generates first names from popular name databases, organized by gender and culture.

- **Surname Generation**. Randomly generates surnames, organized by culture.

- **Full Name Chaining**. Append one or more names and surnames with an intuitive chainable API.

- **105 Cultures Supported**. Afghan, Albanian, Algerian, American, Angolan, Argentinian, Austrian, Azerbaijani, Bahraini, Bangladeshi, Belgian, Bolivian, Botswanan, Brazilian, British, Bruneian, Bulgarian, Burkinabe, Burundian, Cambodian, Cameroonian, Canadian, Chilean, Chinese, Colombian, Costa Rican, Croatian, Cypriot, Czech, Danish, Djiboutian, Dutch, Ecuadorian, Egyptian, Emirati, Estonian, Ethiopian, Fijian, Filipino, Finnish, French, Georgian, German, Ghanaian, Greek, Guatemalan, Haitian, Honduran, Hong Konger, Hungarian, Icelandic, Indian, Indonesian, Iranian, Iraqi, Irish, Israeli, Italian, Jamaican, Japanese, Jordanian, Kazakh, Korean, Kuwaiti, Lebanese, Libyan, Lithuanian, Luxembourgish, Macanese, Malaysian, Maldivian, Maltese, Mauritian, Mexican, Moldovan, Moroccan, Namibian, Nigerian, Norwegian, Omani, Palestinian, Panamanian, Peruvian, Polish, Portuguese, Puerto Rican, Qatari, Russian, Salvadoran, Saudi, Serbian, Singaporean, Slovenian, South African, Spanish, Sudanese, Swedish, Swiss, Syrian, Taiwanese, Tunisian, Turkish, Turkmen, Uruguayan, Yemeni.

- **Weighted Selection**. Names are weighted by real-world frequency so that common names appear more often than rare ones.

- **Dataset Tiers**. Choose between a compact **lite** dataset (~2 MB, top-500 names per category) or the **full** dataset (~39 MB, complete name lists).

- **ISO 2-Letter Codes**. `to_culture()` and `to_gender()` for converting strings to enums.

- **Deterministic Seeding**. Per-call `get_name(gender, culture, seed)` for reproducible results, generator-level `seed()` / `unseed()` for deterministic sequences, and `name::seed()` for replaying a previous generation.

- **Resource Inspection**. `has_resources()` lets you verify databases loaded before generating.

- **Multi-Instance Support**. Construct independent `ng` instances with their own name databases and random engine.

## Integration

[`namegen.hpp`](https://github.com/dasmig/name-generator/blob/master/dasmig/namegen.hpp) is the single required file [released here](https://github.com/dasmig/name-generator/releases). You need to add

```cpp
#include <dasmig/namegen.hpp>

// For convenience.
using ng = dasmig::ng;
```

to the files you want to generate names and set the necessary switches to enable C++23 (e.g., `-std=c++23` for GCC and Clang).

Additionally you must supply the name generator with the [`resources`](https://github.com/dasmig/name-generator/tree/master/resources) folder also available in the [release](https://github.com/dasmig/name-generator/releases).

## Usage

Due to the necessity of supporting multiple culture characters and the way `std::string` works on Windows, this library uses `std::wstring` for all generated names.

When requesting a name for the first time the library will attempt to load the resource files (the default path is `./resources`). Manually load the resources folder if it's in a different location, or use `load(dataset)` to pick between the **lite** and **full** tiers.

```cpp
#include <dasmig/namegen.hpp>

using ng = dasmig::ng;

// Load the lite dataset (~2 MB, top-500 names per category).
ng::instance().load(dasmig::dataset::lite);

// Or load the full dataset (~39 MB, complete name lists).
// ng::instance().load(dasmig::dataset::full);

// Manually load a custom directory if necessary.
// ng::instance().load("path/to/resources");

// Generate a name of any culture and any gender.
std::wstring name = ng::instance().get_name();

// Generate a female name of any culture.
std::wstring female_name = ng::instance().get_name(dasmig::gender::f);

// Generate a male Bulgarian name.
std::wstring bulgarian = ng::instance().get_name(dasmig::gender::m,
                                                  dasmig::culture::bulgarian);

// Generate a full name: first + two surnames.
std::wstring full = ng::instance().get_name().append_surname().append_surname();

// Access individual parts.
auto n = ng::instance().get_name().append_surname();
for (const auto& part : n.parts())
    std::wcout << part << L" ";

// Deterministic generation — same seed always produces the same name.
auto seeded = ng::instance().get_name(dasmig::gender::m,
                                       dasmig::culture::american, 42);

// Replay a previous name using its seed.
auto replay = ng::instance().get_name(dasmig::gender::m,
                                       dasmig::culture::american,
                                       seeded.seed());

// Seed the engine for a deterministic sequence.
ng::instance().seed(100);
// ... generate names ...
ng::instance().unseed(); // restore non-deterministic state

// Independent instance with its own databases and engine.
ng my_gen;
my_gen.load("path/to/resources");
std::wstring name2 = my_gen.get_name().append_surname();
```

For the complete feature guide — cultures, chaining, thread safety, seeding, and more — see the **[Usage Guide](doc/usage.md)**.

## Related Libraries

| Library | Description |
|---------|-------------|
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
