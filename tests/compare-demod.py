#!/usr/bin/env python3
"""Compare all decoded records and signal metadata from two rtl_433 builds.

Uses the capture/protocol/demod/ignore conventions of rtl_433_tests, without
depending on its expected JSON matching either build. Unexpected models, record
counts, order, exit codes, and crashes are checked; only timestamps are ignored.

Example (from the source directory):
    python3 tests/compare-demod.py build-base/src/rtl_433 build/src/rtl_433 \
        rtl_433_tests --modes auto classic minmax
"""

import argparse
import concurrent.futures
import json
from pathlib import Path
import shlex
import subprocess


def captures(root, config_dir):
    for reference in sorted((root / "tests").rglob("*.json")):
        if reference.name in ("codes_test.json", "known_false_positives.json"):
            continue
        directory = reference.parent
        if (directory / "ignore").is_file():
            continue
        sample = next((reference.with_suffix(ext) for ext in
                       (".cu8", ".cs8", ".cs16", ".ook")
                       if reference.with_suffix(ext).is_file()), None)
        if sample is None:
            continue
        args = ["-c", "0"]
        protocol_file = directory / "protocol"
        if protocol_file.is_file():
            protocol = protocol_file.read_text().splitlines()[0].strip()
            if protocol:
                config = config_dir / protocol
                args += ["-c", str(config)] if config.is_file() else ["-R", protocol]
        demod_file = directory / "demod"
        if demod_file.is_file():
            args += shlex.split(demod_file.read_text())
        yield sample, args


def decode(binary, args, cwd, timeout):
    result = subprocess.run([str(binary), *args], cwd=cwd, capture_output=True,
                            timeout=timeout, check=False)
    records = []
    for line in result.stdout.splitlines():
        record = json.loads(line)
        record.pop("time", None)
        records.append(record)
    return result.returncode, records


def compare(job, args):
    sample, options, mode = job
    options = [*options]
    if mode != "auto":
        options += ["-Y", mode]
    options += ["-F", "json", "-M", "level", "-r", str(sample)]
    label = f"{sample.relative_to(args.samples)} [{mode}]"
    try:
        before = decode(args.baseline, options, args.samples, args.timeout)
        after = decode(args.candidate, options, args.samples, args.timeout)
    except (subprocess.TimeoutExpired, ValueError) as error:
        return f"FAIL {label}: {error}"
    if before[0] < 0 or after[0] < 0:
        return f"FAIL {label}: crash (baseline={before[0]}, candidate={after[0]})"
    if before != after:
        detail = {"baseline_exit": before[0], "candidate_exit": after[0],
                  "baseline_records": before[1], "candidate_records": after[1]}
        return f"FAIL {label}: {json.dumps(detail, sort_keys=True)}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--config-dir", type=Path)
    parser.add_argument("--modes", nargs="+", choices=("auto", "classic", "minmax"),
                        default=["auto"])
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    args.baseline = args.baseline.resolve()
    args.candidate = args.candidate.resolve()
    args.samples = args.samples.resolve()
    config_dir = (args.config_dir or Path(__file__).resolve().parent.parent / "conf").resolve()
    for binary in (args.baseline, args.candidate):
        if not binary.is_file():
            parser.error(f"Binary not found: {binary}")
    if args.jobs < 1 or args.timeout <= 0:
        parser.error("jobs and timeout must be positive")
    jobs = [(sample, options, mode) for sample, options in captures(args.samples, config_dir)
            for mode in args.modes]
    if not jobs:
        parser.error("No test captures found")
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(lambda job: compare(job, args), jobs)
        for count, failure in enumerate(results, 1):
            if failure:
                print(failure, flush=True)
                failed += 1
            if count % 500 == 0:
                print(f"Compared {count}/{len(jobs)} captures; {failed} differences", flush=True)
    print(f"{len(jobs)} capture comparisons, {failed} differences", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
