#!/usr/bin/env python3
"""
extract_names.py — Pull all available name data from philipperemy/name-dataset
and generate .names files for the name-generator C++ library.

Output format (v2):
    Line 1: ISO alpha-2 country code (lowercase)
    Line 2: gender tag — 'male', 'female', or 'any'
    Lines 3+: name<TAB>weight  (one per line, UTF-8)

    Weights are derived from rank: weight = 1.0 / sqrt(rank).
    Real name frequency distributions follow approximately 1/rank^0.5-0.7
    (US Census best-fit alpha ~0.64). Pure Zipf (1/rank) is far too steep,
    overweighting the #1 name by ~2x and underweighting rank 500 by ~6x.
    The C++ side feeds these directly into std::discrete_distribution.

Prerequisites:
    pip install names-dataset

Usage:
    python scripts/extract_names.py
    python scripts/extract_names.py --top-n 1000
    python scripts/extract_names.py --output-dir resources_new
    python scripts/extract_names.py --dry-run
"""

import argparse
import math
import os
import sys

# ---------------------------------------------------------------------------
# Alpha-2 → folder name mapping (adjective/demonym, lowercase)
# Existing 22 cultures preserved with their original folder names.
# ---------------------------------------------------------------------------
COUNTRY_FOLDER_MAP = {
    "AE": "emirati",
    "AF": "afghan",
    "AL": "albanian",
    "AO": "angolan",
    "AR": "argentinian",
    "AT": "austrian",
    "AZ": "azerbaijani",
    "BD": "bangladeshi",
    "BE": "belgian",
    "BF": "burkinabe",
    "BG": "bulgarian",
    "BH": "bahraini",
    "BI": "burundian",
    "BN": "bruneian",
    "BO": "bolivian",
    "BR": "brazilian",
    "BW": "botswanan",
    "CA": "canadian",
    "CH": "swiss",
    "CL": "chilean",
    "CM": "cameroonian",
    "CN": "chinese",
    "CO": "colombian",
    "CR": "costarican",
    "CY": "cypriot",
    "CZ": "czech",
    "DE": "german",
    "DJ": "djiboutian",
    "DK": "danish",
    "DZ": "algerian",
    "EC": "ecuadorian",
    "EE": "estonian",
    "EG": "egyptian",
    "ES": "spanish",
    "ET": "ethiopian",
    "FI": "finnish",
    "FJ": "fijian",
    "FR": "french",
    "GB": "british",
    "GE": "georgian",
    "GH": "ghanaian",
    "GR": "greek",
    "GT": "guatemalan",
    "HK": "hongkonger",
    "HN": "honduran",
    "HR": "croatian",
    "HT": "haitian",
    "HU": "hungarian",
    "ID": "indonesian",
    "IE": "irish",
    "IL": "israeli",
    "IN": "indian",
    "IQ": "iraqi",
    "IR": "iranian",
    "IS": "icelandic",
    "IT": "italian",
    "JM": "jamaican",
    "JO": "jordanian",
    "JP": "japanese",
    "KH": "cambodian",
    "KR": "korean",
    "KW": "kuwaiti",
    "KZ": "kazakh",
    "LB": "lebanese",
    "LT": "lithuanian",
    "LU": "luxembourgish",
    "LY": "libyan",
    "MA": "moroccan",
    "MD": "moldovan",
    "MO": "macanese",
    "MT": "maltese",
    "MU": "mauritian",
    "MV": "maldivian",
    "MX": "mexican",
    "MY": "malaysian",
    "NA": "namibian",
    "NG": "nigerian",
    "NL": "dutch",
    "NO": "norwegian",
    "OM": "omani",
    "PA": "panamanian",
    "PE": "peruvian",
    "PH": "filipino",
    "PL": "polish",
    "PR": "puertorican",
    "PS": "palestinian",
    "PT": "portuguese",
    "QA": "qatari",
    "RS": "serbian",
    "RU": "russian",
    "SA": "saudi",
    "SD": "sudanese",
    "SE": "swedish",
    "SG": "singaporean",
    "SI": "slovenian",
    "SV": "salvadoran",
    "SY": "syrian",
    "TM": "turkmen",
    "TN": "tunisian",
    "TR": "turkish",
    "TW": "taiwanese",
    "US": "american",
    "UY": "uruguayan",
    "YE": "yemeni",
    "ZA": "southafrican",
}


def write_names_file(path, country_code, gender_tag, ranked_names):
    """Write a single .names file in the v2 weighted format.

    Args:
        ranked_names: list of (name, true_rank) tuples, sorted by rank ascending.

    Format:
        Line 1: ISO alpha-2 code (lowercase)
        Line 2: 'male', 'female', or 'any'
        Lines 3+: name<TAB>weight  (UTF-8, sqrt-Zipf-derived from true rank)
    """
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(f"{country_code}\n")
        f.write(f"{gender_tag}\n")
        for name, true_rank in ranked_names:
            weight = 1.0 / math.sqrt(true_rank)
            f.write(f"{name}\t{weight:.6f}\n")


def is_ascii_latin(name):
    """Return True if the name contains only ASCII-range characters.

    Filters out Arabic, Cyrillic, CJK, etc. entries that are script-duplicate
    versions of an already-present romanized form (e.g. محمد alongside Mohamed).
    Accented Latin characters (é, ö, ñ, etc.) are kept — they are above ASCII 127
    but belong to Latin Extended blocks (U+00C0–U+024F).
    """
    return all(
        c.isascii() or "\u00c0" <= c <= "\u024f"
        for c in name
    )


def filter_contaminated_surnames(sur_names, first_names_dict, last_names_dict, cc):
    """Remove given names that leaked into the surname list.

    A surname is dropped if BOTH conditions are true:
      1. Its first-name gender skew > 0.8  (strongly gendered = real given name)
      2. Its first-name rank is at least 2x better than its surname rank
         (much more popular as a first name than as a surname)

    This preserves legitimate dual-use names:
      - Chinese surnames (Wang, Li) → near 50/50 gender → kept
      - Arabic/Nigerian patronymics (Mohamed, Ibrahim) → surname rank ≥ first-name rank → kept
    while catching contamination:
      - Hungarian (László, Tamás, Gábor) → 99% male + first-name rank far better → dropped
    """
    clean = []
    dropped = []
    for name in sur_names:
        fn_entry = first_names_dict.get(name)
        ln_entry = last_names_dict.get(name)
        if fn_entry and cc in fn_entry.get("rank", {}):
            gender = fn_entry.get("gender", {})
            m = gender.get("M", 0)
            f = gender.get("F", 0)
            skew = max(m, f) if (m + f) > 0 else 0

            fn_rank = fn_entry["rank"][cc]
            sur_rank = ln_entry["rank"][cc] if (ln_entry and cc in ln_entry.get("rank", {})) else 0

            if skew > 0.8 and sur_rank > 0 and fn_rank * 2 < sur_rank:
                dropped.append(name)
                continue
        clean.append(name)
    return clean, dropped


# Countries whose source lists hold another country's names: rows of the
# second country's data filed under the first. Found by comparing every
# country's top 100 names with every other's: Iran's lists hold Czech names
# (70 of the top 100 female names: Jana, Petra, Kateřina...), the only
# cross-language case among the cultures; the others share a language
# (US/CA, AR/UY) or a migrant population (the Gulf states).
CONTAMINATION = {"IR": "CZ"}


def filter_contamination(names, raw_dict, cc):
    """Drop names the contaminating country ranks better than this one.

    A name of both cultures stays when it ranks better here (Sara in Iran).
    """
    other = CONTAMINATION.get(cc)
    if not other:
        return names, []
    clean, dropped = [], []
    for name in names:
        ranks = raw_dict.get(name, {}).get("rank", {})
        if other in ranks and (cc not in ranks or ranks[other] < ranks[cc]):
            dropped.append(name)
        else:
            clean.append(name)
    return clean, dropped


def resolve_ranks(names, raw_dict, cc):
    """Look up each name's true per-country rank from the raw pickle dict.

    Returns a list of (name, true_rank) sorted by rank ascending.
    Names not found in the raw dict (shouldn't happen) are dropped.
    """
    ranked = []
    for name in names:
        entry = raw_dict.get(name)
        if entry and cc in entry['rank']:
            ranked.append((name, entry['rank'][cc]))
    ranked.sort(key=lambda x: x[1])
    return ranked


def extract(output_dir, top_n, dry_run):
    try:
        from names_dataset import NameDataset
    except ImportError:
        print("ERROR: names-dataset not installed. Run: pip install names-dataset")
        sys.exit(1)

    print("Loading name dataset (requires ~3.2 GB RAM)...")
    nd = NameDataset()

    country_codes = sorted(nd.get_country_codes(alpha_2=True))
    print(f"Dataset contains {len(country_codes)} countries: {', '.join(country_codes)}")

    # Check for unmapped country codes.
    unmapped = [cc for cc in country_codes if cc not in COUNTRY_FOLDER_MAP]
    if unmapped:
        print(f"WARNING: No folder mapping for: {', '.join(unmapped)}")
        print("         These countries will be skipped.")

    totals = {"countries": 0, "files": 0, "names": 0}

    for cc in country_codes:
        folder_name = COUNTRY_FOLDER_MAP.get(cc)
        if not folder_name:
            continue

        folder_path = os.path.join(output_dir, folder_name)

        # ----- Extract male first names -----
        males_raw = nd.get_top_names(
            n=top_n, gender="Male", use_first_names=True, country_alpha2=cc
        )
        male_names = males_raw.get(cc, {}).get("M", [])

        # ----- Extract female first names -----
        females_raw = nd.get_top_names(
            n=top_n, gender="Female", use_first_names=True, country_alpha2=cc
        )
        female_names = females_raw.get(cc, {}).get("F", [])

        # ----- Extract surnames -----
        sur_raw = nd.get_top_names(
            n=top_n, use_first_names=False, country_alpha2=cc
        )
        surname_names = sur_raw.get(cc, [])

        # Strip whitespace, drop empty/blank entries, filter non-Latin scripts.
        male_names = [n.strip() for n in male_names if n.strip() and is_ascii_latin(n.strip())]
        female_names = [n.strip() for n in female_names if n.strip() and is_ascii_latin(n.strip())]
        surname_names = [n.strip() for n in surname_names if n.strip() and is_ascii_latin(n.strip())]

        # Remove given names that leaked into surname data.
        surname_names, sur_dropped = filter_contaminated_surnames(
            surname_names, nd.first_names, nd.last_names, cc
        )
        if sur_dropped:
            print(f"    {cc} dropped contaminated surnames: {', '.join(sur_dropped)}")

        # Remove another country's names filed under this one.
        for label, names, table in (("male", male_names, nd.first_names),
                                    ("female", female_names, nd.first_names),
                                    ("surname", surname_names, nd.last_names)):
            names[:], dropped = filter_contamination(names, table, cc)
            if dropped:
                print(f"    {cc} dropped {len(dropped)} {label} names of {CONTAMINATION[cc]}: "
                      f"{', '.join(dropped[:12])}{'...' if len(dropped) > 12 else ''}")

        # Resolve true per-country ranks from the raw pickle data.
        male_ranked = resolve_ranks(male_names, nd.first_names, cc)
        female_ranked = resolve_ranks(female_names, nd.first_names, cc)
        sur_ranked = resolve_ranks(surname_names, nd.last_names, cc)

        if not male_ranked and not female_ranked and not sur_ranked:
            print(f"  {cc:>2} ({folder_name}): no data — skipped")
            continue

        country_files = 0
        country_names = 0

        if male_ranked:
            country_files += 1
            country_names += len(male_ranked)
        if female_ranked:
            country_files += 1
            country_names += len(female_ranked)
        if sur_ranked:
            country_files += 1
            country_names += len(sur_ranked)

        label = (
            f"  {cc:>2} ({folder_name}): "
            f"{len(male_ranked)} M, {len(female_ranked)} F, "
            f"{len(sur_ranked)} sur  ({country_names} total)"
        )
        print(label)

        if not dry_run:
            os.makedirs(folder_path, exist_ok=True)

            if male_ranked:
                write_names_file(
                    os.path.join(folder_path, "male.names"),
                    cc.lower(), "male", male_ranked,
                )
            if female_ranked:
                write_names_file(
                    os.path.join(folder_path, "female.names"),
                    cc.lower(), "female", female_ranked,
                )
            if sur_ranked:
                write_names_file(
                    os.path.join(folder_path, "sur.names"),
                    cc.lower(), "any", sur_ranked,
                )

        totals["countries"] += 1
        totals["files"] += country_files
        totals["names"] += country_names

    print()
    if dry_run:
        print("[DRY RUN] No files written.")
    print(
        f"Summary: {totals['countries']} countries, "
        f"{totals['files']} files, "
        f"{totals['names']} total names"
    )


def main():
    parser = argparse.ArgumentParser(
        description="Extract names from philipperemy/name-dataset into .names files."
    )
    parser.add_argument(
        "--output-dir",
        default="resources",
        help="Output directory for .names files (default: resources)",
    )
    parser.add_argument(
        "--top-n",
        type=int,
        default=500,
        help="Max names per category per country (default: 500)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be generated without writing files.",
    )
    args = parser.parse_args()

    extract(args.output_dir, args.top_n, args.dry_run)


if __name__ == "__main__":
    main()
