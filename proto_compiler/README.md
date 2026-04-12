# proto_compiler

The proto_compiler lets you describe rtl_433 decoders as Python classes, similar
to how the flex decoder lets you describe a protocol as a declarative key-value
spec in a `.conf` file. But unlike the flex decoder, which interprets that spec
at runtime, the proto_compiler compiles the Python class into C source code. The
compiler is itself a Python program that inspects the input Python class and
generates a complete `r_device` decoder.

The DSL is inspired by [this conversation](https://github.com/merbanan/rtl_433/pull/3399)
about adding Lua to rtl_433.

For examples, see:

* [protocols/akhan_100F14.py](protocols/akhan_100F14.py)
* [protocols/thermopro_tp211b.py](protocols/thermopro_tp211b.py)
* [protocols/tpms_ford.py](protocols/tpms_ford.py)
* [protocols/lacrosse_tx31u.py](protocols/lacrosse_tx31u.py)
* [protocols/current_cost.py](protocols/current_cost.py)

## Protocol structure

In this language, a decoder is a subclass of `Decoder` (or `Dispatcher` for
multi-decoder modules). The class has five main pieces: a `modulation_config()`
method, a `prepare()` method, field annotations, methods, and `to_json`.

### ModulationConfig

Radio parameters that belong in the `r_device` struct (modulation kind, pulse
timing, limits) are returned from a `modulation_config(self)` method as a
`ModulationConfig` named tuple.

```python
from proto_compiler.dsl import Bits, Decoder, Literal, Modulation, ModulationConfig

class thermopro_tp211b(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="ThermoPro TP211B Thermometer",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=105,
            long_width=105,
            reset_limit=1500,
        )
```

Required fields are `device_name`, `modulation`, `short_width`, `long_width`,
and `reset_limit`. Optional fields are `gap_limit`, `sync_width`, `tolerance`,
and `disabled`.

### prepare() and the bitbuffer pipeline

Bitbuffer preprocessing (preamble search, Manchester decode, inverts, and so on)
is declared in `prepare(self, buf)`. The method takes a `BitbufferPipeline` and
returns it after chaining bit-processing operations:

* `invert()` — `bitbuffer_invert` on the row buffer (or on decoded packet bits
  after `manchester_decode`, depending on position in the chain).
* `reflect()` — per-row `reflect_bytes`.
* `find_repeated_row(min_repeats, min_bits, max_bits=…)` — locate a repeated row;
  `max_bits` allows a length upper bound instead of an exact `min_bits` match.
* `search_preamble(pattern, bit_length=None, scan_all_rows=False)` — search for
  a bit pattern; `bit_length` defaults to `pattern.bit_length()` when omitted.
  Use `scan_all_rows=True` when the match may appear on any row (e.g. TPMS).
* `skip_bits(n)` — adjust the bit offset after the search: positive skips
  forward, negative skips backward (with a sanity check in C).
* `manchester_decode(max_bits=0)` — Manchester-decode from the current row and
  offset into a temporary buffer; subsequent field bits are read from that
  decoded stream.

For row-selection strategies (such as scanning multiple rows for a valid
candidate), use `Rows[FirstValid(bits), SubProtocol]` as a field type rather
than a pipeline step (see "Rows" below).

Here's an example:

```python
    def prepare(self, buf):
        return buf.search_preamble(0x552DD4, bit_length=24).skip_bits(24)
```

This prepares the bitbuffer for parsing it into bit fields.

### Fields

Bit fields are parsed automatically. They're declared as class annotations using
the `Bits`, `Literal`, `Repeat`, and `Rows` type constructors:

```python
    id: Bits[24]
    flags: Bits[4]
    temp_raw: Bits[12]
    fixed_aa: Literal[0xAA, 8]
    checksum: Bits[16]
```

The compiler converts these into bit-extraction code at the corresponding
offsets. `Bits[n]` extracts an `n`-bit unsigned integer into a local C variable
of the same name. `Literal[value, n]` extracts `n` bits and asserts that they
equal `value`, returning `DECODE_FAIL_SANITY` on mismatch.  Fields whose names
start with `_` are skipped (their bits are consumed but no variable is emitted).

### Rows (multi-row bitbuffer)

`Repeat[count, SubProtocol]` parses the same sub-layout multiple times along the
single extracted `b[]` buffer. `Rows[rows_tuple, SubProtocol]` instead parses
that sub-layout once per listed bitbuffer row index, using
`bitrow_get_bits(bitbuffer->bb[row], …)` for each row. Optional third argument:
`((row_index, exact_bits), …)` emits a `bits_per_row[row]` length check per pair.

The compiler declares `int parent_subfield[BITBUF_ROWS]` for each sub-field and
only writes indices present in `rows_tuple` (sparse indexing). In expressions,
use `FieldRef('parent_subfield')[row_index]` (or a `def foo(self, parent_subfield): …`
parameter bound to that array). `Rev8(x)` emits `reverse8(x)` in C.

The sub-protocol must not contain nested `Rows` or `Repeat` fields. To scan
multiple rows for the first valid candidate, use the field-spec form
`Rows[FirstValid(bits), SubProtocol]`.

### Variants

Nested `Variant` subclasses split decoding after the parent protocol’s fields.
Each variant must define `when(self, …) -> bool` with explicit parameters for
every field or row-array column the predicate uses (same style as properties).
The body must compile to C; a non-zero value selects that branch (`if` /
`else if` in the emitted decoder).

### Methods and properties

**Merge rule:** A user-defined callable **with** a return annotation (e.g.
`-> int`, `-> float`, `-> str`) is treated as a **property** and emitted as a
C local (`int name = expr;` or an extern call if the body is `...`). A callable
**without** a return annotation is a **method** (e.g. `validate`) and is
emitted in the method phase. Properties are emitted **after** methods, so a
method body cannot reference a property that is defined later — use fields or
duplicate the expression.

Class methods are compiled to C. The compiler only understands simple arithmetic
and bit manipulation expressions, and translates these to C expressions that
look almost identical to the Python expressions. For example, the method


```python
    def temperature_c(self) -> float:
        return (self.temp_raw - 500) * 0.1
```

compiles to

```c
float temperature_c = ((temp_raw - 0x1f4) * 0.1);
```

When the body is unspecified (using `...`), the compiler emits
a call to an external C function that you provide in a companion `.h` file.
For example in the class `thermopro_tp211b`,

```python
    def validate(self, b, checksum): ...
```

compiles to

```c
int valid = thermopro_tp211b_validate(b, checksum);
```

The parameter list is inferred from the method's explicit arguments. If no
explicit arguments are given, the compiler passes all extracted fields
automatically.

The `validate` method is handled slightly differently from other methods: if its
body is a DSL expression and that expression is false, the decode returns
`DECODE_FAIL_SANITY`. If `validate` is external (body `...`), the helper must
return `0` on success or a `DECODE_FAIL_*` / `DECODE_ABORT_*` code on failure.

### `validate_*` hooks and `DecodeFail`

Any method whose name starts with `validate_` (but not the bare name `validate`)
is a **validation hook**. Hooks are emitted **after** all ordinary methods and
**after** bare `validate`, and **before** properties and `data_make`. Hook names
are ordered **lexicographically**, so choose names accordingly when checks must
run in a fixed order (for example `validate_mic` before `validate_sanity_humidity`).

The last parameter must be `fail_value=DecodeFail.*` with a **default** only;
the compiler reads that default and emits `return DECODE_FAIL_MIC`,
`return DECODE_FAIL_SANITY`, `return DECODE_ABORT_EARLY`, or
`return DECODE_ABORT_LENGTH` when an **inline** hook’s expression is false.
`fail_value` is **not** passed in generated C calls.

- **Inline hook:** signature is `(self, fail_value=DecodeFail.MIC)` only (no other
  parameters). The body must be a single DSL expression that is **true** when the
  packet is OK. Emitted as `if (!(expr)) return <token>;`.
- **External hook:** any extra parameters (e.g. `decoder`, field names). Emitted
  as `int vret_<name> = prefix_<name>(...); if (vret_<name> != 0) return vret_<name>;`
  (variant decoders use `prefix_<Variant>_<name>`). Implement the helper in C;
  the Python method can stub with `...`.

`DecodeFail` is re-exported from `proto_compiler` for use in hook signatures.

### `LfsrDigest8`

`LfsrDigest8(b0, b1, …, gen=0x98, key=0x3e)` builds an expression that calls
rtl_433’s `lfsr_digest8` (`bit_util.h`) on the given byte expressions, for example:

```c
(lfsr_digest8((uint8_t const[]){b0, b1, b2}, 3, 0x98, 0x3e))
```

Use it inside `validate_*` (or properties) for MIC-style digests.

### to_json

By default the compiler emits a `data_make` call that outputs every non-private
field and every property. You can override `to_json` to change that behavior and
explicitly specify every key names, label, data type, and formatting to be
emitted:

```python
    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model",        "",          "Ford",           "DATA_STRING"),
            JsonRecord("id",           "",          self.sensor_id,   "DATA_STRING", fmt="%08x"),
            JsonRecord("pressure_PSI", "Pressure",  self.pressure_psi, "DATA_DOUBLE"),
            JsonRecord("mic",          "Integrity", "CHECKSUM",       "DATA_STRING"),
        ]
```

Each `JsonRecord` maps to one entry in the generated `data_make(...)` call. The
`fmt` parameter emits a `DATA_FORMAT` directive.

To emit a row only under certain conditions, use variant dispatch: define
multiple `Variant` classes whose `when` methods gate on the predicate and whose
`to_json` lists include only the applicable fields.  See `acurite_01185m.py`
for an example that splits on which temperature probes are plugged in.

## Code reuse through inheritance

Because protocols are plain Python classes, you can use class inheritance to
reuse code between decoders.  

The CurrentCost TX decoder in `current_cost.py` uses two different framings
("EnviR" and "Classic") that share the same payload structure and decoding logic
but differ in their preamble, bit offset after the match, and the JSONs they
emit. A base class captures everything they have in common:

```python
from proto_compiler.dsl import Bits, Decoder, JsonRecord, Modulation, ModulationConfig, Variant

class current_cost_base(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="CurrentCost Current Sensor",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=250,
            long_width=250,
            reset_limit=8000,
        )

    msg_type: Bits[4]
    device_id: Bits[12]

    class meter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 0

        ch0_valid: Bits[1]
        ch0_power: Bits[15]
        ch1_valid: Bits[1]
        ch1_power: Bits[15]
        ch2_valid: Bits[1]
        ch2_power: Bits[15]

        def power0_W(self, ch0_valid, ch0_power) -> int:
            return ch0_valid * ch0_power

        def power1_W(self, ch1_valid, ch1_power) -> int:
            return ch1_valid * ch1_power

        def power2_W(self, ch2_valid, ch2_power) -> int:
            return ch2_valid * ch2_power

    class counter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 4

        _unused: Bits[8]
        sensor_type: Bits[8]
        impulse: Bits[32]
```

The two concrete decoders inherit from this base and supply their own
`prepare()` pipelines (and JSON overrides), and a `Dispatcher` class chains
them at runtime:

```python
class current_cost_envir(current_cost_base):
    def prepare(self, buf):
        return (
            buf
            .invert()
            .search_preamble(0x55555555A457, bit_length=48)
            .skip_bits(47)
            .manchester_decode()
        )

    class meter(current_cost_base.meter):
        def to_json(self) -> list[JsonRecord]:
            return [ # ...
            ]

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            return [ # ...
            ]


class current_cost_classic(current_cost_base):
    def prepare(self, buf):
        return (
            buf
            .invert()
            .search_preamble(0xCCCCCCCE915D, bit_length=45)
            .skip_bits(45)
            .manchester_decode()
        )

    class meter(current_cost_base.meter):
        def to_json(self) -> list[JsonRecord]:
            return [ # ...
            ]

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            return [ # ...
            ]


class current_cost(Dispatcher):
    def modulation_config(self):
        return ModulationConfig(
            device_name="CurrentCost Current Sensor",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=250,
            long_width=250,
            reset_limit=8000,
        )

    def dispatch(self):
        return (current_cost_envir(), current_cost_classic())
```

The field definitions, `when()` predicates, variant dispatch, and computed properties
(`power0_W`, etc.) are inherited from `current_cost_base`.  While the Python
code avoids duplication, the compiler is not smart enough to avoid
duplication in the emitted code. It generates duplicate code paths for
`current_cost_envir` and `current_cost_classic`. This creates a small amount of
bloat in the binary, which I'm leaving for future work to address.

## Building

To generate the C code, the CMake files run

```
python proto_compiler/build.py proto_compiler/protocols/thermopro_tp211b.py src/devices/thermopro_tp211b.c
```

This loads the protocol module, reads its module-level `decoders` tuple, and
emits one concatenated C source file. Each protocol module ends with
`decoders = (foo(),)` where `foo` is the `Decoder` or `Dispatcher` to compile.

## Other ideas considered

Other tooling for automatically parsing bitstreams was evaluated before settling
on this DSL:

[Hammer](https://github.com/UpstandingHackers/hammer) is a C library for
bit-level binary parsing. The syntax is verbose, it duplicates a lot of the bit
parsing functionality already in rtl_433, and is missing quite a bit of it.

[Kaitai Struct](https://kaitai.io/) is a YAML-based binary format DSL that
compiles to C++. It lacks strong bit-level support. Notably, there is no obvious
way to introduce preamble scans.

[Spicy](https://docs.zeek.org/projects/spicy/en/latest/) is a C++-like DSL for
network protocol parsing that compiles to C++. It is good for parsing byte-level
protocols, but lacks tidy semantics for parsing bitstreams.

[DaeDaLus](https://github.com/GaloisInc/daedalus) is a grammar-based DSL that
compiles to C++. Its syntax was appealing and helped inform the syntax of this
DSL. The repo itself is unfriendly and uninformative, which made it seem like a
poor fit for the rtl_433 community.

[bitmatch](https://git.sr.ht/~cryo/bitmatch/tree/main/item/README.md) is a
small C regexp library for matching and parsing over bitstreams. The regexps
are not very readable and it cannot associate names to fields.

[Construct](https://construct.readthedocs.io/) is a binary parser library in
Python with great semantics, but it is interpreted. Its recently added compiler
targets Python, not C, so it is too slow for our purposes.

This DSL was derived instead to have exactly the semantics rtl_433 needs, and to
compile directly to C.
