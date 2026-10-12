# Data and Method

Where the name lists come from, how they were built and what they leave out. For using the library, see the [usage guide](usage.md).

[TOC]

## Source

Every list comes from [name-dataset](https://github.com/philipperemy/name-dataset) by Philippe Rémy (Apache-2.0 repository), which counts first and last names per country. Its README says the names were "extracted from the Facebook massive dump (533M users)", the 2021 leak of Facebook account data. The library ships no personal data: only names, their country and a weight derived from their popularity rank, never a combination of first and last name from one account.

The extraction script is `scripts/extract_names.py` (it needs `pip install names-dataset` and about 3.2 GB of memory). For each of the 105 countries it has a culture for, it takes the top names by gender and the top surnames, then:

1. Keeps names written in Latin script only (ASCII and Latin-1 Supplement to Latin Extended-B). The dataset lists the same name in several scripts (محمد beside Mohamed); this keeps the romanised form.
2. Removes given names that leaked into the surname list: a "surname" is dropped when, as a first name, it is more than 80% one gender and ranks at least twice as high as it does as a surname. Chinese surnames (near 50/50) and patronymic surnames (Mohamed, Ibrahim) stay.
3. Weights each name by its country rank: `1 / sqrt(rank)`. Real name frequencies fall roughly as `1 / rank^0.5` to `1 / rank^0.7` (the US Census best fit is about 0.64), so plain Zipf (`1 / rank`) would make the top name about twice too likely.

Ranks are those of the full dataset, before filtering, so a list whose top names were in another script starts below rank 1 (Brazilian male names start at Lucas, rank 2; Korean male names at rank 1,016, which is why `lite` has none).

## Tiers

| Tier | Cultures | Names | Per list | Size |
| --- | --- | --- | --- | --- |
| `lite` | 105 | 133,009 | at most 500 (median 500 forenames, 463 surnames) | 3 MB |
| `full` | 105 | 2,273,752 | everything kept (median 5,756 male, 3,741 female, 12,941 surnames) | 41 MB |

`lite` keeps the names among each country's top 500 (by the source's rank, so a list is shorter when some of them were filtered out); every `lite` list is the start of the `full` one. `lite` has no Korean male list. Every list has a female, male and surname file in `full`.

Version 2 removed two folders that version 1 never loaded (`australian`, `ukranian`: country codes the library has no culture for; the cultures were dropped in v1's 2.0.0 for lack of upstream data).

## File Format

```
br
male
Lucas	0.707107
Marcos	0.500000
```

Line 1 is the country code, line 2 the list (`male`, `female`, or `any` for surnames), then one name per line with an optional weight (default 1). Files are UTF-8; a line starting with `#` is a comment. The same format is used for layers, with a third column, `disabled`, to remove a name.

## Generation Model

Every call draws from streams split from the call seed:

1. **Gender** (`"gender"`): male or female with equal probability, unless given.
2. **Culture** (`"culture"`): uniform over the cultures that have a list of that kind loaded, in culture order, unless given. Cultures are not weighted by population; the persona pipeline passes the culture of the chosen country.
3. **Name** (`"name"`): weighted by the list's weights.

A surname draws its gender too (the gender `append_name()` would use). A part appended to a name draws from the stream `"part"`, then the part's position, of the name's seed.

## Known Limitations

- **Romanised names only.** Names in Arabic, Cyrillic, CJK, Hangul and other scripts are dropped, so the lists show how names are written in Latin script, which for some countries (Korea, China, the Gulf) is a minority form.
- **Social-network users, not populations.** The source counts Facebook accounts around 2019-2021: it over-represents younger, urban and connected people and the names popular among them, and it has no birth-year information (names are not weighted by generation).
- **Noise.** Account names include nicknames, business names and typos, and the rank filter and Latin filter are heuristics: a list can contain an odd entry (for example a word used as a name on the platform).
- **No naming conventions.** A full name is the parts you append, space-separated: no family-name-first order, double surnames or patronymics. These are planned (see the project plan).
- **The weights are a model** of the rank-frequency curve, not observed frequencies.
