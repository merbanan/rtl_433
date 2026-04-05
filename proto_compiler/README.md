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

In this language, a decoder is a subclass of `Protocol`. The class has five
main pieces: a `modulation_config` block, a `prepare()` method, field
annotations, methods, and `to_json`. 

### ModulationConfig

Radio parameters that belong in the `r_device` struct (modulation kind, pulse
timing, limits) live in a nested `modulation_config` class that inherits from
`ModulationConfig`. 

```python
from proto_compiler.dsl import Bits, Literal, Modulation, ModulationConfig, Protocol

class thermopro_tp211b(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "ThermoPro TP211B Thermometer"
        output_model = "ThermoPro-TP211B"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 105
        long_width = 105
        reset_limit = 1500
```

### prepare() and the bitbuffer pipeline

Bitbuffer preprocessing (preamble search, Manchester decode, inverts, and so on)
is declared in `prepare()`. This method chains bit process operations that include:

* `invert()` — `bitbuffer_invert` on the row buffer (or on decoded packet bits
  after `manchester_decode`, depending on position in the chain).
* `reflect()` — per-row `reflect_bytes`.
* `find_repeated_row(min_repeats, min_bits, max_bits=…)` — locate a repeated row;
  `max_bits` allows a length upper bound instead of an exact `min_bits` match.
* `first_valid_row(bits, reflect=False, checksum_over_bytes=0)` — try each row in
  order until one has exactly *bits* (optional per-row reflect and add-with-carry
  checksum vs the next byte); use this instead of `find_repeated_row` when the
  slicer gives multiple candidates and validation picks the first good row. Must
  be the last pipeline step.
* `search_preamble(pattern, bit_length=None, scan_all_rows=False)` — search for
  a bit pattern; `bit_length` defaults to `pattern.bit_length()` when omitted.
  Use `scan_all_rows=True` when the match may appear on any row (e.g. TPMS).
* `skip_bits(n)` — adjust the bit offset after the search: positive skips
  forward, negative skips backward (with a sanity check in C).
* `manchester_decode(max_bits=0)` — Manchester-decode from the current row and
  offset into a temporary buffer; subsequent field bits are read from that
  decoded stream.

Here's an example:

```python
    def prepare(self):
        return self.bitbuffer.search_preamble(0x552DD4, bit_length=24).skip_bits(24)
```

This prepares the bitbuffer for parsing it into bit fields.

### Fields

Bit fields are parsed automatically. They're declared as class annotations using
the `Bits`, `Literal`, `Cond`, and `Repeat` type constructors:

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

The `validate` method is handled slightly differently from other methods: if it
returns False, the method aborts early with `DECODE_FAIL_SANITY`.

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

You can conditionally emit a row by supplying a `when` parameter, which emits a
`DATA_COND` guard so the field only appears when the condition is true:

```python
            JsonRecord("humidity", "Humidity", self.humidity, "DATA_INT",
                        when=self.has_humidity),
```

This compiles to a row in `data_make` that looks like this:

```c
"humidity", "Humidity", DATA_COND, has_humidity, DATA_INT, humidity,
```

## Code reuse through inheritance

Because protocols are plain Python classes, you can use class inheritance to
reuse code between decoders.  

The CurrentCost TX decoder in `current_cost.py` uses two different framings
("EnviR" and "Classic") that share the same payload structure and decoding logic
but differ in their preamble, bit offset after the match, and the JSONs they
emit. A base class captures everything they have in common:

```python
from proto_compiler.dsl import Bits, F, JsonRecord, Modulation, ModulationConfig, Protocol, Variant

class current_cost_base(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "CurrentCost Current Sensor"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 250
        long_width = 250
        reset_limit = 8000

    msg_type: Bits[4]
    device_id: Bits[12]

    class meter(Variant):
        when = F.msg_type == 0
        ch0_valid: Bits[1]
        ch0_power: Bits[15]
        ch1_valid: Bits[1]
        ch1_power: Bits[15]
        ch2_valid: Bits[1]
        ch2_power: Bits[15]

        def power0_W(self) -> int:
            return self.ch0_valid * self.ch0_power

        def power1_W(self) -> int:
            return self.ch1_valid * self.ch1_power

        def power2_W(self) -> int:
            return self.ch2_valid * self.ch2_power

    class counter(Variant):
        when = F.msg_type == 4
        _unused: Bits[8]
        sensor_type: Bits[8]
        impulse: Bits[32]
```

The two concrete decoders inherit from this base and supply their own
`prepare()` pipelines (and JSON overrides):

```python
class current_cost_envir(current_cost_base):
    def prepare(self):
        return (
            self.bitbuffer
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
    def prepare(self):
        return (
            self.bitbuffer
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
```

The field definitions, variant dispatch logic, and computed properties
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

This loads the protocol module, finds the root `Protocol` subclass(es), compiles
each one, and writes the resulting C file. If a module defines multiple root
protocols (as `current_cost.py` does), one C file is generated per protocol.

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
