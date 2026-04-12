# proto_compiler

The proto_compiler lets you describe rtl_433 decoders as Python classes, similar
to how the flex decoder lets you describe a protocol as a declarative key-value
spec in a `.conf` file. But unlike the flex decoder, which interprets that spec
at runtime, the proto_compiler compiles the Python class into C source code. The
compiler is itself a Python program that looks at the instances of the classes
you've authored, and generates a complete `r_device` decoder.

The compiler is _not_ a generic Python to C compiler. Instead, it converts a
data structure, which happens to be an instance of a Pythong class, to C code.
That means you get to use all the nicities of Python to populate your data
structure, like inheritance, loop comprehension, etc.

The DSL is inspired by [this conversation](https://github.com/merbanan/rtl_433/pull/3399)
about adding Lua to rtl_433.

For examples, see:

* [protocols/akhan_100F14.py](protocols/akhan_100F14.py)
* [protocols/thermopro_tp211b.py](protocols/thermopro_tp211b.py)
* [protocols/tpms_ford.py](protocols/tpms_ford.py)
* [protocols/lacrosse_tx31u.py](protocols/lacrosse_tx31u.py)
* [protocols/current_cost.py](protocols/current_cost.py)

## Protocol structure

In this language, a decoder is a subclass of `Decoder` (or `Dispatcher` for a
multi-framing device).  The class has five main pieces: a `modulation_config()`
method that fills the `r_device` struct, a `prepare()` method that describes
the bitbuffer pipeline, bit-field declarations as class annotations, methods
that derive values from those fields, and an optional `to_json` that shapes
the `data_make` call.

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
    …
```


### prepare() and the bitbuffer pipeline

You define all the bitbuffer preprocessing (preamble search, Manchester
decode, inversion, and so on) in a method called `prepare`. The method transforms the bitbuffer by applying the following operations in the specified order
and updating a `row` and `offset` variable that describe from which row and which bit offset of the resulting bitbuffer to start parsing:

* `invert()` emits `bitbuffer_invert(bitbuffer);`, flips every bit in every
  row.
* `reflect()` applies per-row `reflect_bytes`, bit-reversing each byte.
* `find_repeated_row(min_repeats, min_bits, max_bits=None)` locates a repeated
  row and pins the tip's `row` to it. With `max_bits=None` the length must
  match `min_bits` exactly; otherwise the length must be `<= max_bits`.
* `search_preamble(pattern, bit_length=None, scan_all_rows=False)` searches for
  a bit pattern and advances the tip's `offset` to just after the match.
  `bit_length` defaults to `pattern.bit_length()`. Use `scan_all_rows=True`
  when the match may appear on any row. The compiler emits an outer
  row-scan loop and records which row matched.
* `skip_bits(n)` adjusts the tip's `offset` by `n` bits. Positive values skip
  forward; negative values skip backward. The C runtime checks that the offset
  does not underflow.
* `manchester_decode(max_bits=0)` decodes Manchester-encoded bits.

Here's an example:

```python
class thermopro_tp211b(Decoder):
    …

    def prepare(self, buf):
        return buf.search_preamble(0x552DD4, bit_length=24).skip_bits(24)

    …
```


### Fields

Bit fields are parsed automatically. They're declared fields in your Protocol class using
type annotations like `Bits`, `Literal`, `Repeat`, and `Rows` type constructors:

```python
class thermopro_tp211b(Decoder):
    …

    id: Bits[24]
    flags: Bits[4]
    temp_raw: Bits[12]
    fixed_aa: Literal[0xAA, 8]
    checksum: Bits[16]
```

The compiler converts these into bit-extraction code at the corresponding
offsets. `Bits[n]` extracts an `n`-bit unsigned integer.  `Literal[value, n]`
extracts `n` bits and asserts that they equal `value`, returning
`DECODE_FAIL_SANITY` on mismatch.  Fields whose names start with `_` are
skipped.

### Methods

A method is a Python function defined in the decoder class.  Methods are simple
Python arithmetic expressions that are directly compiled to C. They act like
computed counterparts of fields. They can be used like fields in most places,
but they're expressions that derive their value from bit fields that were
parsed, or from other methods.

For example:

```python
class thermopro_tp211b(Decoder):
    …

    def temperature_c(self, temp_raw: int) -> float:
        return (temp_raw - 500) * 0.1
```

compiles to a function definition

```c
static inline float thermopro_tp211b_temperature_c(int temp_raw) {
    return ((temp_raw - 0x1f4) * 0.1);
}
```

and, wherever `self.temperature_c` appears in `to_json`, a call like this is emitted:

```c
"temperature_C", "", DATA_DOUBLE, (double)thermopro_tp211b_temperature_c(temp_raw)
```

TODO: introduce here the set of operators that an expression can use. you can say "in addition to the usual arithmetic operators (-,+, &, etc), you can use predefined operators like Reverse8, ..."

#### External methods

If the method is too complicated to express as a simple Python arithmetic expression, you can specify it as a C function in a companion
header file. In that case, you still have to define the signature of the method in Python, but you can give it a bare body using the Python
ellipsis literal `...`. We call such methods "external".

For example,

```python
class thermopro_tp211b(Decoder):
    …
    def validate_checksum(self, id: int, flags: int, temp_raw: int, checksum: int,
                          fail_value=DecodeFail.MIC) -> bool: ...
```

The compiler writes `#include "thermopro_tp211b.h"` into the generated C.
The header is expected to define
`static inline bool thermopro_tp211b_validate_checksum(int id, int flags, int temp_raw, int checksum)`
and is checked in under `src/devices/<stem>.h`.

### `validate_*` hooks and `DecodeFail`

A method whose name starts with `validate_` is a *validation hook*.  Hooks
share the method machinery, with two twists:

1. The final parameter of a validation hook must be `fail_value=DecodeFail.*`
   with a default.  The compiler reads that default to decide which
   `DECODE_*` token to return on failure.  `fail_value` is not passed in the
   generated C call.
2. The call is emitted *automatically* — the user never invokes a hook from
   `to_json` or anywhere else.  As soon as every field declared in the
   hook's signature has been parsed, the compiler emits a guard that exits
   with the `fail_value` token if the hook returns false.

For example, given

```python
    sensor_id: Bits[8]
    checksum: Bits[8]

    def validate_checksum(self, sensor_id, checksum,
                          fail_value=DecodeFail.MIC) -> bool:
        return ((sensor_id & 0xFF) + checksum) & 0xFF == 0
```

the compiler emits the hook definition near the top of the file and, inside
the decode function, injects the guard right after both dependencies are
in scope:

```c
static inline bool prefix_validate_checksum(int sensor_id, int checksum) {
    return (((sensor_id & 0xff) + checksum) & 0xff) == 0;
}
/* … */
int sensor_id = bitrow_get_bits(b, bit_pos, 8);
bit_pos += 8;
int checksum = bitrow_get_bits(b, bit_pos, 8);
bit_pos += 8;
if (!prefix_validate_checksum(sensor_id, checksum))
    return DECODE_FAIL_MIC;
```

As with regular methods, replacing the body with `...` turns the hook
*external*: the compiler emits only a declaration and the user writes the
implementation in the companion header.  `DecodeFail` is re-exported from
`proto_compiler` for use in hook signatures.

### Repeat (variable-length sub-layouts in one row)

Some payloads contain a variable number of same-shape records back-to-back
in the same row.  The `Repeat[count_expr, SubProtocol]` field spec handles this. The resulting code
parses `SubProtocol`'s layout `count_expr` times in a row and stores the result
in parallel arrays.

For example,

```python
class sensor_reading(Repeatable):
    sensor_type: Bits[4]
    reading: Bits[12]

class lacrosse_tx31u(Decoder):
    …
    measurements: Bits[3]
    readings: Repeat[FieldRef("measurements"), sensor_reading]
```

Each sub-field becomes a C array sized by `count_expr`, populated inside a
`for` loop that advances `bit_pos` by the sub-layout width per iteration:

```c
int readings_sensor_type[measurements];
int readings_reading[measurements];
for (int _i = 0; _i < measurements; _i++) {
    readings_sensor_type[_i] = bitrow_get_bits(b, bit_pos, 4);
    bit_pos += 4;
    readings_reading[_i] = bitrow_get_bits(b, bit_pos, 12);
    bit_pos += 12;
}
```

`count_expr` may be an integer literal or the name of a field
wrapped in `FieldRef(…)`.  The `SubProtocol`
class must inherit from `Repeatable` and may declare only `Bits[]` and
`Literal[]` fields. Deeply nested Repeatable's aren't supposed.

### Rows (fields that live on specific bitbuffer rows)

Some protocols spread information across multiple rows of the raw
bitbuffer rather than packing everything into one.  The `Rows[row_spec, SubProtocol]` handles
these situations. It parses a
sub-layout once for each row specified in `row_spec`:

```python
class row_layout(Repeatable):
    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    b3: Bits[8]
    b4: Bits[4]

class alectov1(Decoder):
    …
    cells: Rows[(1, 2, 3, 4, 5, 6), row_layout, ((1, 36),)]
```

`row_spec` can be either either:

- **A tuple of row indices** `(row1, row2, …)`. The emitted code parses exactly those rows.
- **`FirstValid(bits)`**. The emitted code scans every row for the first one whose length
  equals `bits` and for which no `validate_*` hook fails.

In methods, you can reference the resulting fields like with `cells[1].b0`.

### Variants

Some protocols change layout depending on a header field. For example, a weather sensor
can have a "message_id" field that determines whether the rest of the packet has 
humidity readings or temperature readings.
You can express these variable packets in Python using
a `Variant` subclasses nested inside the parent
`Decoder`.  At decode time the compiler extracts the parent's fields first,
then evaluates each variant's `when()` predicate to decide which variant to actually parse.

Here's an example:

```python
class current_cost_base(Decoder):
    …
    msg_type: Bits[4]
    device_id: Bits[12]

    class meter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 0

        ch0_valid: Bits[1]
        ch0_power: Bits[15]
        ch1_valid: Bits[1]
        ch1_power: Bits[15]

        def power0_W(self, ch0_valid, ch0_power) -> int:
            return ch0_valid * ch0_power

    class counter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 4

        sensor_type: Bits[8]
        impulse: Bits[32]
```

compiles to something like

```c
int msg_type = bitrow_get_bits(b, bit_pos, 4);  bit_pos += 4;
int device_id = bitrow_get_bits(b, bit_pos, 12); bit_pos += 12;
if ((msg_type == 0)) {
    int ch0_valid = bitrow_get_bits(b, bit_pos, 1);  bit_pos += 1;
    /* … meter body, data_make, return 1; */
} else if ((msg_type == 4)) {
    int sensor_type = bitrow_get_bits(b, bit_pos, 8); bit_pos += 8;
    /* … counter body, data_make, return 1; */
}
return DECODE_FAIL_SANITY;
```

Each variant must define `when(self, …) -> bool`. If this method returns true, the
Variant is selected. 
If no variant's `when` matches, the decode function returns `DECODE_FAIL_SANITY`.

Variants may not contain nested `Rows[]` or `Repeat[]`.

A variant inherits parent fields and parent methods for reference in its
`when`, its methods, and its `to_json`.  

### to_json

By default the compiler emits a `data_make` call that outputs every non-private
field and every method (except special ones like `validate_*` methods).  You can
override `to_json` to change this behavior. You can explicitly specify the key
name, label, data type, and formatting to be emitted.

For example, your decoder might supply

```python
    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model",        "",          "Ford",           "DATA_STRING"),
            JsonRecord("id",           "",          self.sensor_id,   "DATA_STRING", fmt="%08x"),
            JsonRecord("pressure_PSI", "Pressure",  self.pressure_psi, "DATA_DOUBLE"),
            JsonRecord("mic",          "Integrity", "CHECKSUM",       "DATA_STRING"),
        ]
```

Each `JsonRecord` maps to one entry in the generated `data_make(…)` call. The
`fmt` parameter emits a `DATA_FORMAT` directive.

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

    ...
```

The two concrete decoders inherit from this base and supply their own
`prepare()` pipelines and JSON overrides


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
            return [ # …
            ]

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            return [ # …
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
            return [ # …
            ]

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            return [ # …
            ]
```


Then a `Dispatcher` class chains them at runtime:

```python
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
