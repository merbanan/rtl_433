"""Exercise rtl_433's actual C device decoder, without importing Home Assistant.

All binaries and generated signals belong to an invocation-owned temporary
directory. Compact synthetic examples are included beside this script.
Use --baseline to reproduce the pre-fix overflow rather than run acceptance tests.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = json.loads(Path(__file__).with_name("golden_output.json").read_text())
SYNTHETIC = json.loads(Path(__file__).with_name("synthetic_examples.json").read_text())


def encode(words: list[int]) -> str:
    """Construct a derived PCM row; this is not a live SDR recording."""
    bits = ""
    for index, word in enumerate(words):
        pad = int(index == 0)
        parity = (word.bit_count() & 1) ^ pad
        symbols = f"{word:08b}{pad}{parity}1"
        bits += "1110" + "".join("10" if bit == "1" else "01" for bit in symbols)
    return bits


def code(bits: str) -> str:
    packed = bits + "0" * (-len(bits) % 8)
    payload = int(packed or "0", 2).to_bytes(len(packed) // 8, "big").hex()
    return f"{{{len(bits)}}}{payload}"


def build(source: Path, harness: Path, output: Path, sanitize: bool) -> None:
    files = [harness, source / "src/devices/proflame2.c"]
    files += [
        source / "src" / name
        for name in ("bitbuffer.c", "bit_util.c", "decoder_util.c", "data.c", "abuf.c", "output_file.c", "term_ctl.c")
    ]
    args = ["cc", "-std=c99", "-D_GNU_SOURCE", "-g", "-O1", "-I", str(source / "include")]
    if sanitize:
        args += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([*args, *map(str, files), "-o", str(output)], check=True)


class DecoderTests(unittest.TestCase):
    binary: Path
    rtl433: Path | None = None

    def decode(self, bits: str, accepted: bool = True) -> dict | None:
        process = subprocess.run([str(self.binary), code(bits)], check=False, capture_output=True, text=True)
        self.assertNotIn("Sanitizer", process.stderr, process.stderr)
        self.assertNotIn("runtime error:", process.stderr, process.stderr)
        if accepted:
            self.assertEqual(process.returncode, 0, process.stderr)
            return json.loads(process.stdout)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertEqual(process.stdout, "")
        return None

    def test_golden_json_and_terminal_bits(self):
        for expected in GOLDEN.values():
            bits = encode(list(bytes.fromhex(expected["raw_words"])))
            for row in (bits, bits[:-1], bits + "0"):
                with self.subTest(variant=expected["protocol_variant"], length=len(row)):
                    self.assertEqual(self.decode(row), expected)

    def test_pretty_layout_uses_original_control_columns(self):
        columns = (0, 25, 51, 77)
        for variant, expected in GOLDEN.items():
            row = encode(list(bytes.fromhex(expected["raw_words"])))
            process = subprocess.run([str(self.binary), code(row), "kv"], check=False, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertNotIn("Sanitizer", process.stderr)
            lines = [line for line in process.stdout.splitlines() if line and not line.startswith("_")]
            keys = [
                ["time", None, "Variant"],
                ["model", None, "Id"],
                ["Cmd1", "Cmd2", "Err1", "Err2"] if variant == "legacy" else ["Cmd1", "Cmd2", "W6", "W7"],
            ]
            if variant == "extended":
                keys.append(["W8", "W9", "W10"])
            keys += [
                ["Pilot", "Light", "Thermostat", "Power"],
                ["Front", "Fan", "Aux", "Flame"],
                ["Integrity", None, "Raw words"],
            ]
            self.assertEqual(len(lines), len(keys), process.stdout)
            for line, row_keys in zip(lines, keys, strict=True):
                for index, key in enumerate(row_keys):
                    if key is not None:
                        self.assertEqual(line.index(key), columns[index], process.stdout)
            self.assertIn("Pilot     : -1" if variant == "extended" else "Pilot     : 0", process.stdout)
            self.assertEqual(lines[-1].index("Raw words"), lines[-2].index("Aux"))

    def test_synthetic_extended_examples(self):
        for example in SYNTHETIC["extended"]:
            words = bytes.fromhex(example["words"])
            bits = encode(list(words))
            for row in (bits, bits[:-1], bits + "0"):
                with self.subTest(example=example["name"], length=len(row)):
                    actual = self.decode(row)
                    self.assertEqual(actual["raw_words"], example["words"])
                    self.assertEqual(actual["id"], int.from_bytes(words[:3], "big"))
                    self.assertEqual(actual["protocol_variant"], "extended_10_word")
                    self.assertEqual(actual["pilot"], -1)
                    self.assertEqual(actual["mic"], "DIGEST")

    def test_synthetic_legacy_examples(self):
        for example in SYNTHETIC["legacy"]:
            words = bytes.fromhex(example["words"])
            bits = encode(list(words))
            for row in (bits, bits[:-1], bits + "0"):
                with self.subTest(example=example["name"], length=len(row)):
                    actual = self.decode(row)
                    self.assertEqual(actual["raw_words"], example["words"])
                    self.assertEqual(actual["id"], int.from_bytes(words[:3], "big"))
                    self.assertEqual(actual["protocol_variant"], "legacy_7_word")
                    self.assertEqual(actual["pilot"], words[3] >> 7)
                    self.assertEqual(actual["mic"], "PARITY")

    def test_invalid_lengths_and_extended_tail_never_becomes_legacy(self):
        words = list(bytes.fromhex(GOLDEN["extended"]["raw_words"]))
        for count in (0, 1, 6, 8, 9, 11, 30):
            with self.subTest(count=count):
                self.decode(encode((words * 3)[:count]), accepted=False)
        bits = encode(words)
        for offset in range(7 * 26, len(bits)):
            with self.subTest(tail_bit=offset):
                changed = bits[:offset] + str(1 - int(bits[offset])) + bits[offset + 1 :]
                self.decode(changed, accepted=False)
        for length in range(183, 259):
            self.decode(bits[:length], accepted=False)

    def test_bad_sync_padding_parity_guards_and_internal_manchester(self):
        for expected in GOLDEN.values():
            bits = encode(list(bytes.fromhex(expected["raw_words"])))
            for offset in range(len(bits)):
                with self.subTest(variant=expected["protocol_variant"], bit=offset):
                    changed = bits[:offset] + str(1 - int(bits[offset])) + bits[offset + 1 :]
                    self.decode(changed, accepted=False)

    def test_integrity_mutations_with_valid_word_parity(self):
        words = list(bytes.fromhex(GOLDEN["extended"]["raw_words"]))
        for index in range(10):
            for bit in range(8):
                with self.subTest(word=index, bit=bit):
                    changed = words.copy()
                    changed[index] ^= 1 << bit
                    self.decode(encode(changed), accepted=False)

    def test_only_narrow_terminal_repair_and_gap(self):
        for expected in GOLDEN.values():
            bits = encode(list(bytes.fromhex(expected["raw_words"])))
            for row in (bits[:-2], bits[:-1] + "1", bits + "1", bits + "00", bits + "0000"):
                self.decode(row, accepted=False)

    def test_actual_detector_slicer_and_c_decoder(self):
        if self.rtl433 is None:
            self.skipTest("Supply --rtl433-binary for detector/PCM/C-decoder replay")
        # Synthesized constant-carrier CU8, derived from protocol words. This
        # checks the software pipeline, not reception from a physical remote.
        for expected in GOLDEN.values():
            bits = "0" * 120 + (encode(list(bytes.fromhex(expected["raw_words"]))) + "0" * 12) * 5 + "0" * 240
            iq = bytearray()
            for index, bit in enumerate(bits):
                samples = round((index + 1) * 104.25) - round(index * 104.25)
                iq.extend(bytes((240 if bit == "1" else 128, 128)) * samples)
            with TemporaryDirectory(prefix="proflame2-iq.") as temp:
                path = Path(temp) / "derived_315M_250k.cu8"
                path.write_bytes(iq)
                command = [str(self.rtl433), "-c", "0", "-R", "207", "-s", "250k", "-r", str(path)]
                result = subprocess.run([*command, "-F", "json"], check=False, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("Sanitizer", result.stderr)
                records = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertTrue(records, result.stderr)
                for actual in records:
                    self.assertEqual({key: actual[key] for key in expected}, expected)
                result = subprocess.run([*command, "-F", "csv"], check=False, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                records = list(csv.DictReader(io.StringIO(result.stdout)))
                self.assertTrue(records, result.stderr)
                for actual in records:
                    self.assertEqual(actual["protocol_variant"], expected["protocol_variant"])
                    self.assertEqual(actual["raw_words"], expected["raw_words"])
                    self.assertEqual(actual["mic"], expected["mic"])
                    self.assertEqual(actual["pilot"], str(expected["pilot"]))
                result = subprocess.run([*command, "-F", "kv"], check=False, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = result.stdout.splitlines()
                time_line = next(line for line in lines if line.startswith("time"))
                controls = next(line for line in lines if line.startswith("Front"))
                integrity = next(line for line in lines if line.startswith("Integrity"))
                self.assertEqual(time_line.index("Variant"), controls.index("Aux"))
                self.assertEqual(integrity.index("Raw words"), controls.index("Aux"))
                pilot_line = next(line for line in lines if line.startswith("Pilot"))
                self.assertTrue(pilot_line.startswith(f"Pilot     : {expected['pilot']}"))
                # Silence and low-amplitude deterministic noise must not decode.
                for data in (bytes((128, 128)) * 60000, bytes(126 + i % 5 for i in range(120000))):
                    path.write_bytes(data)
                    result = subprocess.run([*command, "-F", "json"], check=False, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(), "")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT, help="rtl_433 source checkout (default: this checkout).")
    parser.add_argument("--decoder-binary", type=Path, help="Use an already-built decoder harness.")
    parser.add_argument("--harness-source", type=Path, help="Checkout containing tests/proflame2-test.c.")
    parser.add_argument("--baseline", action="store_true", help="Expect the historical oversized-frame ASan failure.")
    parser.add_argument("--no-sanitize", action="store_true")
    parser.add_argument("--rtl433-binary", type=Path, help="Built rtl_433 for actual detector/PCM pipeline replay.")
    parser.add_argument(
        "--build-pipeline", action="store_true", help="Build rtl_433 with CMake and run CTest plus replay."
    )
    args = parser.parse_args()
    for path in (args.decoder_binary, args.rtl433_binary):
        if path is not None and not path.is_file():
            parser.error(f"Built executable does not exist: {path}")
    harness = (args.harness_source or args.source) / "tests/proflame2-test.c"
    with TemporaryDirectory(prefix="proflame2-c-decoder.") as temp:
        binary = args.decoder_binary or Path(temp) / "proflame2-test"
        if args.build_pipeline:
            build_dir = Path(temp) / "cmake"
            log = Path(temp) / "cmake.log"
            with log.open("w") as output:
                commands = [
                    [
                        "cmake",
                        "-S",
                        str(args.source),
                        "-B",
                        str(build_dir),
                        "-DCMAKE_BUILD_TYPE=Debug",
                        "-DENABLE_RTLSDR=OFF",
                        "-DENABLE_SOAPYSDR=OFF",
                    ],
                    ["cmake", "--build", str(build_dir), "--parallel", "4"],
                ]
                for command in commands:
                    process = subprocess.run(command, check=False, stdout=output, stderr=subprocess.STDOUT)
                    if process.returncode:
                        output.flush()
                        print(log.read_text()[-12000:])
                        return 1
            subprocess.run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"], check=True)
            args.rtl433_binary = build_dir / "src" / "rtl_433"
        if args.decoder_binary is None:
            build(args.source, harness, binary, not args.no_sanitize)
        elif args.baseline:
            parser.error("--baseline must compile its harness with sanitizers")
        if args.baseline:
            if args.no_sanitize:
                parser.error("--baseline requires sanitizers")
            words = list(bytes.fromhex(SYNTHETIC["extended"][0]["words"]))
            process = subprocess.run([str(binary), code(encode(words))], check=False, capture_output=True, text=True)
            if process.returncode == 0 or "AddressSanitizer: stack-buffer-overflow" not in process.stderr:
                raise RuntimeError("Historical oversized-frame overflow was not reproduced")
            print("Confirmed historical stack-buffer-overflow with synthetic data under AddressSanitizer.")
            return 0
        DecoderTests.binary = binary
        DecoderTests.rtl433 = args.rtl433_binary
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(DecoderTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
