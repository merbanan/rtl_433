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

Nested `Variant` subclasses split decoding after the parent protocol's fields.
Each variant must define `when(self, …) -> bool` with explicit parameters for
every field (parent or variant) the predicate uses — the same declaration
style as methods.  The body must be a DSL expression; a non-zero value
selects that branch (`if` / `else if` in the emitted decoder).  Variant
subclasses may declare their own `Bits[]` / `Literal[]` fields and their own
methods, which are extracted and emitted only on the branch where `when`
holds.  Variants may not contain nested `Rows[]` or `Repeat[]`.  If no
variant's `when` matches the decode function returns `DECODE_FAIL_SANITY`.

A variant inherits parent fields and parent methods for reference in its
`when`, its methods, and its `to_json`.  Parent-level methods are emitted
once (parent-prefixed) and called from variant bodies without the variant
prefix; variant-level methods are emitted once per variant (variant-prefixed).
`acurite_01185m.py` demonstrates conditional field output by splitting into
four variants rather than using a run-time `DATA_COND` guard.

### Methods

A method is any non-`validate_*` callable defined in the decoder (or variant)
class body.  Every method must declare explicit parameters (naming the fields
or other callables whose results it consumes) and an explicit return type
annotation — the compiler rejects the class at import time if either is
missing.

The compiler emits every method as a `static inline` C function defined ahead
of the decode function, and calls it directly at each use site.  There is no
intermediate C local for a method's result; the function is inlined wherever
the value is needed.  For example:

```python
    def temperature_c(self, temp_raw: int) -> float:
        return (temp_raw - 500) * 0.1
```

compiles to a function definition

```c
static inline float thermopro_tp211b_temperature_c(int temp_raw) {
    return ((temp_raw - 0x1f4) * 0.1);
}
```

and, wherever `self.temperature_c` appears in `to_json`, a call such as

```c
"temperature_C", "", DATA_DOUBLE, (double)thermopro_tp211b_temperature_c(temp_raw)
```

The return annotation picks the C type: `int` → `int`/`DATA_INT`, `float` →
`float`/`DATA_DOUBLE`, `str` → `const char *`/`DATA_STRING`.

A method may reference the result of another method — the compiler inlines
the nested calls.  For example if a second method takes `cmd` as a parameter
and `cmd` is itself a method defined on the class, the call site emits
`outer(inner_cmd_prefix(…))` automatically.

Parameters may be field names declared on the class, results of other
callables, or (for validate hooks only — see below) the special
`fail_value=DecodeFail.*`.  Names that cannot be resolved to a field, a
callable, or a `BitbufferPipeline` parameter raise a compile-time error.

#### External methods

When the body is a bare `...` (Ellipsis), the method is *external*: the
compiler emits only a `static inline` forward declaration and the user
supplies the definition in a companion header.  For example, for
`thermopro_tp211b`:

```python
    def validate_checksum(self, id: int, flags: int, temp_raw: int, checksum: int,
                          fail_value=DecodeFail.MIC) -> bool: ...
```

the compiler writes `#include "thermopro_tp211b.h"` into the generated C.
The header is expected to define
`static inline bool thermopro_tp211b_validate_checksum(int id, int flags, int temp_raw, int checksum)`
and is checked in under `src/devices/<stem>.h`.

### `validate_*` hooks and `DecodeFail`

A method whose name starts with `validate_` is a *validation hook*.  Hooks
share the method machinery — explicit parameters, explicit return type (must
be `-> bool`), inline vs. external selection — plus two extras:

1. The final parameter must be `fail_value=DecodeFail.*` with a default.  The
   compiler reads that default to decide which `DECODE_*` token to return on
   failure.  `fail_value` is not passed in the generated C call.
2. The call is emitted *automatically* — the user never writes it.  The
   codegen walks each hook's declared parameters (excluding `fail_value`) as
   dependencies and emits `if (!hook(...)) return <token>;` at the earliest
   point in the decode function where every dependency is in scope.  When
   several hooks become eligible at the same checkpoint, the codegen orders
   them lexicographically.

Inside a `FirstValid` row loop the return is replaced by
`result = <token>; continue;`, so a failing row is skipped while the outer
loop retries the next row.

Example:

```python
    def validate_checksum(self, sensor_id: int, checksum: int,
                          fail_value=DecodeFail.MIC) -> bool:
        return ((sensor_id & 0xFF) + checksum) & 0xFF == 0
```

compiles to a `static inline bool prefix_validate_checksum(int sensor_id, int checksum)`
definition and an automatic guard

```c
if (!prefix_validate_checksum(sensor_id, checksum))
    return DECODE_FAIL_MIC;
```

placed right after `sensor_id` and `checksum` are both extracted.

As with regular methods, replacing the body with `...` turns the hook
*external*: the compiler emits only a declaration and the user writes the
implementation in the companion header.  `DecodeFail` is re-exported from
`proto_compiler` for use in hook signatures.

### `LfsrDigest8`

`LfsrDigest8(b0, b1, …, gen=0x98, key=0x3e)` builds an expression that calls
rtl_433’s `lfsr_digest8` (`bit_util.h`) on the given byte expressions, for example:

```c
(lfsr_digest8((uint8_t const[]){b0, b1, b2}, 3, 0x98, 0x3e))
```

Use it inside `validate_*` or any method for MIC-style digests.

### to_json

By default the compiler emits a `data_make` call that outputs every non-private
field and every non-`validate_*` method.  For a variant the model string comes
from the parent decoder's `device_name` and parent fields/methods are included
before the variant's own.  You can override `to_json` to change that behaviour
and explicitly specify every key name, label, data type, and formatting to be
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

The field definitions, `when()` predicates, variant dispatch, and computed methods
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
