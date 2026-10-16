# Data and Method

Where the name lists come from, how they were built and what they leave out. For using the library, see the [usage guide](usage.md).

[TOC]

## Source

Every list comes from [name-dataset](https://github.com/philipperemy/name-dataset) by Philippe Rémy (Apache-2.0 repository), which counts first and last names per country. Its README says the names were "extracted from the Facebook massive dump (533M users)", the 2021 leak of Facebook account data. The library ships no personal data: only names, their country and a weight derived from their popularity rank, never a combination of first and last name from one account.

The extraction script is `scripts/extract_names.py` (it needs `pip install names-dataset` and about 3.2 GB of memory). For each of the 105 countries it has a culture for, it takes the top names by gender and the top surnames, then:

1. Keeps names written in Latin script only (ASCII and Latin-1 Supplement to Latin Extended-B). The dataset lists the same name in several scripts (محمد beside Mohamed); this keeps the romanised form.
2. Removes initials, abbreviations and fragments, which the source holds because it counts tokens of account names. A name is dropped when it has no vowel letter (Md for Mohammed, Sk for Sheikh, Hj for Haji, Jp, Ck, and keyboard noise such as Xcvbn); accented vowels and ø, æ, œ and ı count as vowels. Real names without a vowel letter stay: the Hokkien, Teochew and Cantonese surnames Ng, Chng, Png, Tng, Sng, Hng, Kng, Khng and Thng, pinyin Lv (Lü) and the Cantonese syllable Tsz, and, in Czech, Croatian, Serbian and Slovenian, names whose vowel is a syllabic r or l (Vlk, Smrž, Krč, Črt). In the cultures whose names are romanised from Arabic, Persian, Bengali or Thaana script (the Arab League countries, Iran, Afghanistan, Bangladesh and the Maldives), names under three letters are dropped as well: no romanised name there is that short, and the source's are particles and kunyas split from a longer name (Al, El, Om, Um) or truncations (Ma, Sa, Mo). Md had been the top male name in Bangladesh and four Gulf states, and Om the top female name in Libya. The rules are `is_real_name` in the script; they remove 915 of 132,485 names from `lite` and 19,251 of 2,261,005 from `full`, the most from one list being 54 (Moroccan surnames).
3. Removes given names that leaked into the surname list: a "surname" is dropped when, as a first name, it is more than 80% one gender and ranks at least twice as high as it does as a surname. Chinese surnames (near 50/50) and patronymic surnames (Mohamed, Ibrahim) stay.
4. Removes another country's names filed under this one. Comparing every country's top 100 names with every other's found one such case among the cultures: Iran's lists held Czech names (70 of the top 100 female names, from Jana and Petra on, and Czech surnames such as Nováková). Iran's lists drop every name the source ranks better in Czechia than in Iran (129 male, 238 female and 156 surnames among the top 500); a name of both cultures that ranks better in Iran, such as Sara, stays. The other overlaps the comparison found are countries that share a language (the United States and Canada, Argentina and Uruguay) or a migrant population (South Asian names in the Gulf states), and are kept. The pairs are in the script's `CONTAMINATION`.
5. Weights each name by its country rank: `1 / sqrt(rank)`. Real name frequencies fall roughly as `1 / rank^0.5` to `1 / rank^0.7` (the US Census best fit is about 0.64), so plain Zipf (`1 / rank`) would make the top name about twice too likely.

Ranks are those of the full dataset, before filtering, so a list whose top names were in another script starts below rank 1 (Brazilian male names start at Lucas, rank 2; Korean male names at rank 1,016, which is why `lite` has none).

## Tiers

| Tier | Cultures | Names | Per list | Size |
| --- | --- | --- | --- | --- |
| `lite` | 105 | 131,570 | at most 500 (median 495 forenames, 463 surnames) | 2.3 MB |
| `full` | 105 | 2,241,754 | everything kept (median 5,561 male, 3,715 female, 12,745 surnames) | 43 MB |

`lite` keeps the names among each country's top 500 (by the source's rank, so a list is shorter when some of them were filtered out); every `lite` list is the start of the `full` one. `lite` has no Korean male list. Every list has a female, male and surname file in `full`.

Version 3 removed two folders that version 2 never loaded (`australian`, `ukranian`: country codes the library has no culture for; the cultures were dropped in 2.0.0 for lack of upstream data, but their files stayed).

## File Format

```
br
male
Lucas	0.707107
Marcos	0.500000
```

Line 1 is the country code, line 2 the list (`male`, `female`, or `any` for surnames; `male-surname` and `female-surname` for the surnames of one gender, see below), then one name per line with an optional weight (default 1). Files are UTF-8; a line starting with `#` is a comment. The same format is used for layers, with a third column, `disabled`, to remove a name.

## Gendered Surnames

Most cultures have one surname list (`sur.names`), used for both genders. In some, a surname changes with gender (Ivanov and Ivanova, Kowalski and Kowalska, Jónsson and Jónsdóttir). Those cultures also have `male.sur.names` and `female.sur.names`. A surname is drawn from the list of the name's gender when the culture has one, and from `sur.names` otherwise. Each gendered list is complete: it holds the forms of its gender and the surnames that do not change (Greek Georgiou), so a draw is a single weighted pick.

Ten cultures have them: Czech, Polish, Russian, Bulgarian, Lithuanian, Icelandic, Greek, Kazakh, Azerbaijani and Turkmen. `scripts/split_surnames.py RESOURCES_DIR` writes the files from `sur.names`, using the suffix rules in the script (in Czech, the e of a final -ek, -ec or -el drops before -ová: Vlček and Vlčková, Němec and Němcová, Pavel and Pavlová): a form the list lacks (Lithuanian has few feminine forms) is derived from its counterpart with the same weight, and entries under three letters are dropped. `sur.names` is not changed, so adding a culture is a new entry in the script's `RULES`.

A layer for a gendered list uses `male-surname` or `female-surname` on its second line, and `layers()` for it is `surname_layers(culture, gender)`. A layer for `any` changes `sur.names` only: it does not reach a culture that has gendered lists, so change those two lists instead.

## Generation Model

Every call draws from streams split from the call seed:

1. **Gender** (`"gender"`): male or female with equal probability, unless given.
2. **Culture** (`"culture"`): uniform over the cultures that have a list of that kind loaded, in culture order, unless given. Cultures are not weighted by population; the persona pipeline passes the culture of the chosen country.
3. **Name** (`"name"`): weighted by the list's weights.

A surname draws its gender too (the gender `append_name()` would use), and in a culture with gendered surnames the name comes from the list of that gender. A part appended to a name draws from the stream `"part"`, then the part's position, of the name's seed.

## Known Limitations

- **Romanised names only.** Names in Arabic, Cyrillic, CJK, Hangul and other scripts are dropped, so the lists show how names are written in Latin script, which for some countries (Korea, China, the Gulf) is a minority form.
- **Social-network users, not populations.** The source counts Facebook accounts around 2019-2021: it over-represents younger, urban and connected people and the names popular among them, and it has no birth-year information (names are not weighted by generation).
- **Noise.** Account names include nicknames, business names and typos, and the rank filter and Latin filter are heuristics: a list can contain an odd entry, such as a word used as a name on the platform. The filter in step 2 removes initials and short fragments, but not longer abbreviations (Mohd for Mohammed) or two-letter tokens outside the Arabic-script cultures that happen to have a vowel (a German surname list has Na and Ha).
- **No naming conventions.** A full name is the parts you append, space-separated: no family-name-first order, double surnames or patronymics. These are planned (see the project plan).
- **The weights are a model** of the rank-frequency curve, not observed frequencies.
