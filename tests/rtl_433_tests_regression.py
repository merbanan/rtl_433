#!/usr/bin/env python3

"""Gate rtl_433 against the rtl_433_tests sample files using a baseline allowlist.

This wrapper drives the rtl_433_tests runner ``bin/run_test.py`` from
https://github.com/merbanan/rtl_433_tests against a freshly built ``rtl_433``
binary, classifies every capture as passing / failing / skipped, and compares
the passing set against a checked-in baseline allowlist.

The sample set is never 100% green and evolves independently, so a naive
"fail if anything fails" gate is useless. Instead we record the set of captures
known to pass at a *pinned* rtl_433_tests commit (``--record`` mode) and, on every CI
run, fail only when a capture in that baseline regresses (now failing or
skipped). Captures that newly start passing are reported -- never fatal -- so a
developer can promote them into the baseline.

Capture identity is the sample-relative path of the reference ``.json`` file,
which is stable under a pinned commit.

See ``tests/rtl_433_tests_baseline.txt`` and
``tests/rtl_433_tests_pinned_sha.txt`` for the gated inputs, and the two
``sample_tests_*`` jobs in ``.github/workflows/check.yml`` for the CI wiring.
"""

import argparse
import os
import re
import subprocess
import sys

# Lines emitted by the rtl_433_tests runner that we parse.
FAIL_RE = re.compile(r"^## Fail with '(.+?)':")
IGNORE_RE = re.compile(r"^WARNING: Ignoring '(.+?)'")
MISSING_RE = re.compile(r"^WARNING: Missing '(.+?)'")


def enumerate_universe(tests_dir):
    """Return the set of every reference JSON, as paths relative to tests_dir."""
    universe = set()
    for root, _dirs, files in os.walk(tests_dir):
        for name in files:
            if name.endswith(".json"):
                full = os.path.join(root, name)
                universe.add(os.path.relpath(full, tests_dir))
    return universe


def to_identity(capture_path, tests_dir):
    """Normalize a runner-reported capture path to its sample-relative .json id.

    The runner reports the input capture (``.cu8``/``.ook``); the baseline keys
    on the sibling ``.json``. Paths may be absolute or relative; we re-root them
    on tests_dir so they match the enumerated universe.
    """
    rel = os.path.relpath(capture_path, tests_dir)
    return os.path.splitext(rel)[0] + ".json"


def run_samples(runner, rtl433, conf, tests_dir):
    """Run the rtl_433_tests runner and return its combined stdout+stderr text."""
    cmd = [
        sys.executable, runner,
        "-c", rtl433,
        "-C", conf,
        "-t", tests_dir,
        "-I", "time",
        "--first-line",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.stdout + proc.stderr


def classify(output, universe, tests_dir):
    """Split the universe into (passing, failing, skipped) sets."""
    failing = set()
    skipped = set()
    for line in output.splitlines():
        m = FAIL_RE.match(line)
        if m:
            failing.add(to_identity(m.group(1), tests_dir))
            continue
        m = IGNORE_RE.match(line) or MISSING_RE.match(line)
        if m:
            skipped.add(to_identity(m.group(1), tests_dir))
    # Only keep identities that are actually part of the universe.
    failing &= universe
    skipped &= universe
    passing = universe - failing - skipped
    return passing, failing, skipped


def read_baseline(path):
    """Read the baseline allowlist, ignoring blanks and # comments."""
    baseline = set()
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                baseline.add(line)
    return baseline


def write_baseline(path, passing):
    """Write the passing set as the baseline, one sorted path per line."""
    with open(path, "w") as fh:
        for ident in sorted(passing):
            fh.write(ident + "\n")


def emit_summary(lines):
    """Echo to stdout and, if running under GitHub Actions, the step summary."""
    text = "\n".join(lines)
    print(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as fh:
            fh.write(text + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Gate rtl_433 against rtl_433_tests via a baseline allowlist.")
    parser.add_argument("--rtl433", required=True,
                        help="path to the built rtl_433 binary")
    parser.add_argument("--conf", required=True,
                        help="path to rtl_433's conf/ directory")
    parser.add_argument("--tests", required=True,
                        help="path to the rtl_433_tests tests/ directory")
    parser.add_argument("--runner", required=True,
                        help="path to the rtl_433_tests bin/run_test.py")
    parser.add_argument("--baseline", required=True,
                        help="path to the baseline allowlist file")
    parser.add_argument("--record", action="store_true",
                        help="write the current passing set to the baseline and exit")
    args = parser.parse_args()

    universe = enumerate_universe(args.tests)
    if not universe:
        print("ERROR: no *.json reference files found under %s" % args.tests,
              file=sys.stderr)
        return 2

    output = run_samples(args.runner, args.rtl433, args.conf, args.tests)
    passing, failing, skipped = classify(output, universe, args.tests)

    # Guard against the binary not running at all: if nothing passed and nothing
    # failed, the runner never executed rtl_433 meaningfully.
    if not passing and not failing:
        print("ERROR: rtl_433_tests runner produced no pass/fail results -- "
              "is the rtl_433 binary path correct?", file=sys.stderr)
        sys.stderr.write(output)
        return 2

    if args.record:
        write_baseline(args.baseline, passing)
        print("Recorded %d passing captures to %s" % (len(passing), args.baseline))
        return 0

    baseline = read_baseline(args.baseline)
    regressions = sorted(baseline & (failing | skipped))
    newly_passing = sorted(passing - baseline)

    lines = [
        "### rtl_433_tests sample gate",
        "",
        "| metric | count |",
        "| --- | --- |",
        "| universe | %d |" % len(universe),
        "| passing | %d |" % len(passing),
        "| failing | %d |" % len(failing),
        "| skipped | %d |" % len(skipped),
        "| baseline | %d |" % len(baseline),
        "| regressions | %d |" % len(regressions),
        "| newly passing | %d |" % len(newly_passing),
    ]
    if regressions:
        lines.append("")
        lines.append("#### Regressions (baseline captures now failing/skipped)")
        lines.extend("- `%s`" % r for r in regressions)
    if newly_passing:
        lines.append("")
        lines.append("#### Newly passing -- promote these")
        lines.append("Add these paths to `%s` (keep sorted) and commit to enforce them:"
                     % args.baseline)
        lines.extend("- `%s`" % n for n in newly_passing)
    emit_summary(lines)

    if regressions:
        print("\nFAIL: %d baseline capture(s) regressed" % len(regressions),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
