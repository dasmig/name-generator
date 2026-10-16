#!/usr/bin/env python3
"""Instruction-count gate for a dasmig library's benchmark.

Runs `bench --gate` (a fixed workload: one load, 1,000 calls of every getter,
one batch of 1,000) under Valgrind's cachegrind and compares the number of
instructions executed with the committed baseline in bench/gate_baseline.txt.
Instruction counts are stable from run to run, unlike wall time on a shared CI
runner. A rise of more than 10% fails; a fall of more than 10% is reported so
the baseline can be lowered.

Usage: scripts/gate.py BENCH_BINARY [--update]
  --update   write the measured count as the new baseline

The baseline belongs to one toolchain (the Linux CI leg: clang 18 with
libstdc++ 14); a count measured elsewhere is not comparable.

This file is identical in every library (tools/check-conformance.sh checks it).
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

LIMIT = 1.10
BASELINE = Path(__file__).resolve().parent.parent / "bench" / "gate_baseline.txt"


def measure(binary):
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            ["valgrind", "--tool=cachegrind", "--cache-sim=no", f"--cachegrind-out-file={Path(tmp) / 'out'}",
             binary, "--gate"], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"gate: the benchmark failed under valgrind ({result.returncode}):\n{result.stderr[-2000:]}")
    match = re.search(r"I\s+refs:\s+([\d,]+)", result.stderr)
    if not match:
        sys.exit(f"gate: no instruction count in valgrind's output:\n{result.stderr[-2000:]}")
    return int(match.group(1).replace(",", ""))


def summary(text):
    print(text)
    target = os.environ.get("GITHUB_STEP_SUMMARY")
    if target:
        with open(target, "a", encoding="utf-8") as out:
            out.write(text + "\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit(__doc__)
    count = measure(args[0])
    if "--update" in sys.argv:
        BASELINE.write_text(f"{count}\n", encoding="utf-8", newline="\n")
        summary(f"gate: baseline set to {count:,} instructions")
        return
    if not BASELINE.exists():
        summary(f"gate: GATE_COUNT={count} (no baseline yet; run scripts/gate.py {args[0]} --update on this toolchain)")
        sys.exit(1)
    baseline = int(BASELINE.read_text(encoding="utf-8").strip())
    ratio = count / baseline
    line = f"gate: {count:,} instructions, baseline {baseline:,} ({ratio:.3f}x)"
    if ratio > LIMIT:
        summary(line + f" - more than {int((LIMIT - 1) * 100)}% above the baseline. "
                "If the rise is intended, run scripts/gate.py --update and commit bench/gate_baseline.txt.")
        sys.exit(1)
    if ratio < 1 / LIMIT:
        line += " - more than 10% below: lower the baseline with --update."
    summary(line)


if __name__ == "__main__":
    main()
