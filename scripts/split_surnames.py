"""Split the surname lists of cultures whose surnames change with gender.

For each such culture, reads <tier>/<culture>/sur.names and writes
male.sur.names and female.sur.names next to it (header lines `male-surname`
and `female-surname`). sur.names is not touched, and a culture that is not
listed in RULES gets no gendered files, so the library falls back to
sur.names for it.

A surname is classified from the list itself:
  * masculine or feminine when a rule links it to a form in the list, or when
    it has the ending of one (STRONG_*), and
  * neutral otherwise (Czech Svoboda, Greek Georgiou, ...): it goes in both
    lists.
A form the list lacks (Lithuanian Kazlauskas has no Kazlauskaite) is derived
with the same weight, so each list is complete.

Usage:
  python split_surnames.py RESOURCES_DIR [--tier lite --tier full]
"""

import argparse
import re
import sys
from pathlib import Path

MIN_LENGTH = 3  # shorter entries are fragments (Sh, Gh, Kz), not surnames

# (masculine ending, feminine ending), longest ending first. A rule links
# stem+masculine to stem+feminine.
SLAVIC_OV = [("ov", "ova"), ("ev", "eva"), ("in", "ina")]
RULES = {
    "czech": [("ský", "ská"), ("cký", "cká"), ("", "ová"), ("a", "ová"), ("ý", "á")],
    "polish": [("ski", "ska"), ("cki", "cka"), ("dzki", "dzka")],
    "russian": [("sky", "skaya"), ("skiy", "skaya"), ("skii", "skaya")] + SLAVIC_OV,
    "bulgarian": [("ski", "ska")] + SLAVIC_OV,
    "kazakh": SLAVIC_OV[:2],
    "azerbaijani": SLAVIC_OV[:2],
    "turkmen": [("ov", "ova"), ("ev", "eva"), ("ow", "owa"), ("ew", "ewa")],
    "icelandic": [("son", "dóttir")],
    "greek": [("akis", "aki"), ("idis", "idi"), ("adis", "adi"), ("os", "ou")],
    "lithuanian": [("auskas", "auskaitė"), ("as", "aitė"), ("ys", "ytė"), ("is", "ytė"),
                   ("us", "utė"), ("ov", "ova")],
}

# Endings that mark a form even when its counterpart is not in the list.
# Czech masculine -á/-ová is feminine; masculine ends in a consonant or -ý,
# which is too loose to classify on, so only paired names are masculine.
STRONG_FEMININE = {
    "czech": ("ová",),
    "polish": ("ska", "cka", "dzka"),
    "russian": ("ova", "eva", "ina", "skaya"),
    "bulgarian": ("ova", "eva", "ina", "ska"),
    "kazakh": ("ova", "eva"),
    "azerbaijani": ("ova", "eva"),
    "turkmen": ("ova", "eva", "owa", "ewa"),
    "icelandic": ("dóttir",),
    "greek": (),
    "lithuanian": ("aitė", "ytė", "utė", "ienė", "ova"),
}
STRONG_MASCULINE = {
    "czech": ("ský", "cký"),
    "polish": ("ski", "cki", "dzki"),
    "russian": ("ov", "ev", "sky", "skiy"),
    "bulgarian": ("ov", "ev", "ski"),
    "kazakh": ("ov", "ev"),
    "azerbaijani": ("ov", "ev"),
    "turkmen": ("ov", "ev", "ow", "ew"),
    "icelandic": ("son",),
    "greek": ("opoulos", "akis", "idis", "adis"),
    "lithuanian": ("as", "is", "ys", "us", "ov"),
}


VOWELS = "aeiouyáéíóúůýěAEIOUY"

# Cultures where the e of a final -ek, -ec or -el drops before the feminine
# ending: Czech Vlček/Vlčková, Němec/Němcová, Pavel/Pavlová.
FLEETING_E = {"czech"}
CONSONANT = f"[^{VOWELS}]"


def read_list(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    code = lines[0].strip()
    rows = {}
    for line in lines[2:]:
        if not line.strip() or line.startswith("#"):
            continue
        name, _, weight = line.partition("\t")
        rows[name] = float(weight) if weight else 1.0
    return code, rows


def counterparts(name, rules, to_feminine, fleeting_e=False):
    """The other-gender forms of `name` that a rule gives, best guess first."""
    out = []
    for masc, fem in rules:
        src, dst = (masc, fem) if to_feminine else (fem, masc)
        # An empty masculine ending (Czech Novák -> Nováková) is for names
        # ending in a consonant: Svoboda becomes Svobodová by its own rule.
        if not masc and to_feminine and name[-1] in VOWELS:
            continue
        if name.endswith(src) and len(name) > len(src):
            stem = name[: len(name) - len(src)]
            if not masc and fleeting_e:
                # Vlček -> Vlčková and back; Beck (c-k) has no fleeting e.
                if to_feminine and re.search(f"{CONSONANT}e[kcl]$", stem):
                    out.append(stem[:-2] + stem[-1] + dst)
                elif (not to_feminine and re.search(f"{CONSONANT}[kcl]$", stem)
                      and not stem.endswith("ck")):
                    out.append(stem[:-1] + "e" + stem[-1] + dst)
            form = stem + dst
            if form not in out:
                out.append(form)
    return out


def known(forms, rows):
    """The first of `forms` that is in the list, or None."""
    return next((f for f in forms if f in rows), None)


def split(culture, rows):
    rules = RULES[culture]
    fleeting = culture in FLEETING_E
    feminine, masculine, neutral = {}, {}, {}
    for name, weight in rows.items():
        if len(name) < MIN_LENGTH:
            continue
        as_masc = counterparts(name, rules, True, fleeting)   # name read as masculine
        as_fem = counterparts(name, rules, False, fleeting)   # name read as feminine
        if as_fem and (known(as_fem, rows) or name.endswith(STRONG_FEMININE[culture])):
            feminine[name] = weight
        elif as_masc and (known(as_masc, rows) or name.endswith(STRONG_MASCULINE[culture])):
            masculine[name] = weight
        else:
            neutral[name] = weight

    # Derive the form a list lacks, at the weight of the form it comes from.
    for name, weight in list(masculine.items()):
        forms = counterparts(name, rules, True, fleeting)
        if not known(forms, rows):
            feminine.setdefault(forms[0], weight)
    for name, weight in list(feminine.items()):
        forms = counterparts(name, rules, False, fleeting)
        if not known(forms, rows):
            masculine.setdefault(forms[0], weight)
    return masculine, feminine, neutral


def write_list(path, code, tag, groups):
    merged = {}
    for group in groups:
        for name, weight in group.items():
            merged[name] = max(weight, merged.get(name, 0.0))
    ordered = sorted(merged.items(), key=lambda kv: (-kv[1], kv[0]))
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(f"{code}\n{tag}\n")
        for name, weight in ordered:
            f.write(f"{name}\t{weight:.6f}\n")
    return len(ordered)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("resources")
    ap.add_argument("--tier", action="append", default=None)
    args = ap.parse_args()
    for tier in args.tier or ["lite", "full"]:
        for culture in RULES:
            path = Path(args.resources) / tier / culture / "sur.names"
            if not path.is_file():
                print(f"skip {tier}/{culture}: no sur.names", file=sys.stderr)
                continue
            code, rows = read_list(path)
            masc, fem, neutral = split(culture, rows)
            m = write_list(path.with_name("male.sur.names"), code, "male-surname", [masc, neutral])
            f = write_list(path.with_name("female.sur.names"), code, "female-surname", [fem, neutral])
            print(f"{tier}/{culture}: {len(rows)} -> masculine {len(masc)}, feminine {len(fem)}, "
                  f"neutral {len(neutral)}; wrote male {m}, female {f}")


if __name__ == "__main__":
    main()
