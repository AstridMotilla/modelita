#!/usr/bin/env python3
"""Run the Modelita conformance suite against any implementation.

    python3 tests/run.py -- racket racket/cli.rkt
    python3 tests/run.py -k bridge -- ./modelita        only cases whose name contains "bridge"
    python3 tests/run.py --write -- racket racket/cli.rkt   (re)write the expected .out files

The implementation is a command that takes a program's path as its last
argument and prints the dump described in tests/README.md. Numbers are
compared with a tolerance; everything else must match exactly.
"""
import argparse
import pathlib
import subprocess
import sys

CASES = pathlib.Path(__file__).parent / "cases"
TOLERANCE = 1e-9


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def same_token(a, b):
    if a == b:
        return True
    x, y = number(a), number(b)
    if x is None or y is None:
        return False
    return abs(x - y) <= TOLERANCE * max(1.0, abs(x), abs(y))


def compare(expected, got):
    """The first difference, as a message, or None."""
    want, have = expected.splitlines(), got.splitlines()
    for n, (a, b) in enumerate(zip(want, have), 1):
        ta, tb = a.split(), b.split()
        if len(ta) != len(tb) or not all(same_token(x, y) for x, y in zip(ta, tb)):
            return f"line {n} of the dump:\n  expected: {a}\n  got:      {b}"
    if len(want) != len(have):
        return f"expected {len(want)} lines of dump, got {len(have)}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-k", default="", help="only run cases whose name contains this")
    parser.add_argument("--write", action="store_true", help="write each case's .out from this implementation")
    parser.add_argument("command", nargs="+", help="the implementation's command")
    args = parser.parse_args()

    failed = 0
    cases = sorted(p for p in CASES.glob("*.mdl") if args.k in p.stem)
    for case in cases:
        try:
            run = subprocess.run(args.command + [str(case)], capture_output=True, text=True)
        except OSError as e:
            print(f"can't run {' '.join(args.command)}: {e}")
            return 2
        expected_file = case.with_suffix(".out")
        if args.write:
            expected_file.write_text(run.stdout)
            continue
        if not expected_file.exists():
            print(f"MISSING {case.stem}: no .out file")
            failed += 1
            continue
        problem = compare(expected_file.read_text(), run.stdout)
        if run.returncode != 0:
            problem = f"exited with {run.returncode}\n{run.stderr.strip()}"
        if problem:
            failed += 1
            print(f"FAIL {case.stem}\n  {problem}")
    if args.write:
        print(f"wrote {len(cases)} expected outputs")
        return 0
    print(f"{len(cases) - failed} of {len(cases)} cases pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
