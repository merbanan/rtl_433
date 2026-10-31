# rtl_433 Proflame2 C decoder checks

This runner compiles and invokes the actual rtl_433 Proflame2 device
decoder. It does not use the Python frame decoder as an acceptance oracle.
The Python standard library and a C compiler are sufficient. Binaries and
derived signals are created in an automatically cleaned temporary directory.

```sh
python3 tests/proflame2/test_decoder.py --build-pipeline
```

AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default. Use
`--no-sanitize` only for an additional ordinary build check, not as a substitute
for sanitizer validation. To reproduce the historical overflow, point `--source`
at a pre-fix checkout and `--harness-source` at the updated checkout containing
`tests/proflame2-test.c`, then add `--baseline`.

For actual pulse-detector, PCM-slicer, and C-device-decoder replay, also provide
`--rtl433-binary /path/to/built/rtl_433`. Without this option, the pipeline test
is explicitly skipped. Alternatively, `--build-pipeline` runs CMake/CTest and
replay in an automatically cleaned build directory; CMake must be on PATH.
Replay signals are synthesized from protocol words,
including silence/noise controls. They are derived test signals, not live SDR
recordings. JSON and CSV output are checked through the actual executable.

`golden_output.json` freezes the JSON contract using synthetic identity
`0x123456`. Compare records by keys and values, not object key order. `id` and
all individual bytes are JSON integers; human-readable hexadecimal formatting
does not change their JSON types. `raw_words` contains exactly 14 or 20 lowercase
hexadecimal characters in on-air word order, with no prefix or separators.
Legacy output uses `protocol_variant=legacy_7_word` and `mic=PARITY`; extended
output uses `protocol_variant=extended_10_word` and `mic=DIGEST`. The latter
describes the documented custom alternating-word calculation, not a CRC.

Legacy previously reported `CHECKSUM` despite checking word parity only. The new
`PARITY` label is an intentional compatibility correction. Legacy command/error
bytes remain available. Extended W6/W7 are not legacy error bytes, so extended
records expose W6-W10, omit `err1` and `err2`, and report integer `pilot=-1`
to mean unknown. Extended W4 bit 7 is not yet a validated pilot mapping. Once
that mapping is established, the decoder and consumers can be updated to use
`0/1`; the current sentinel must not be treated as a pilot state.
Power and flame zero are independent, including power on with flame zero.

Pretty output preserves the original control columns (offsets 0, 25, 51, 77).
`Variant` follows `time` in column 3, and `Raw words` follows `Integrity` in
column 3, exactly under `Aux`. Extended command rows contain `Cmd1/Cmd2/W6/W7`
followed by `W8/W9/W10`. Layout assertions exercise the actual KV formatter,
including output redirected to a pipe; JSON and CSV checks cover the sentinel.

`synthetic_examples.json` contains eight artificial word sequences (three legacy,
five extended), including zero/one byte boundaries, mixed bytes, a serial with
its high bit set, pilot on, and power on with flame zero. No example was copied
from a remote capture or customer report. Serial values were chosen as test
constants; they were not taken from customer remotes.

The fixed extended integrity bytes were calculated using the linear transform's
basis images `21 42 84 08 31 62 c4 88` for input bits 0 through 7. They remain
literal fixture values: the test does not generate expected integrity bytes with
the C decoder's implementation.

PCM rows, truncated terminal guards, one-bit gaps, malformed framing, oversized
inputs, all single-bit row mutations, and payload/integrity mutations with valid
word parity are generated in memory. This keeps checked-in data small without
storing repetitions or large recordings. `--baseline` also uses a synthetic
extended frame to demonstrate the historical overflow. These deterministic
software tests do not establish reception from physical hardware.

CMake/CTest automatically runs the corpus and detector replay when Python 3.10+
is available. The basic C test always runs. For sanitizer validation, invoke this
script directly; `--build-pipeline` also builds and tests the full executable.
No Home Assistant checkout, package, or network access is needed.
