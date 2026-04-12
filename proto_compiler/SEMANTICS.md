# Organization of the compiler:

dsl.py validates the user's program.  it transforms it slightly by creating various annotations described  below.

codegen.py generates the C code. it tries to push as much work as it can to dsl.py


# Proto-compiler formal semantics

This document describes the semantics of the proto-compiler DSL and its code
generation.  The goal is to be precise enough to reimplement the compiler from
scratch.

---

## 1. Top-level structure

A *decoder module* is a Python source file.  At the top level it defines a
`decoders` tuple of *decoder instances*:

```python
decoders = (thermopro_tp211b(), current_cost())
```

The compiler iterates over `decoders` and produces one C source file per
entry.  Because the compiler operates on instances rather than classes,
Python's normal attribute lookup resolves inherited members automatically —
no explicit MRO traversal is needed in the compiler.

A *decoder class* is a Python class that extends `Decoder`.  `Decoder`
inherits from `Protocol`; the class hierarchy is:

```
Protocol
├── Repeatable(Protocol)   — only Bits[]/Literal[] fields allowed
│   └── Variant(Repeatable) — adds when() requirement
└── Decoder(Protocol)      — adds modulation_config and prepare()
```

A decoder instance may contain:

| Element | Python form | Required |
|---|---|---|
| Modulation config | nested `modulation_config(ModulationConfig)` class | yes |
| Pipeline declaration | `def prepare(self, buf: BitBuffer) -> BitBuffer` | yes |
| Field declarations | class-level annotations | zero or more |
| Methods | `def name(self, ...) -> type` | zero or more |
| Validation hooks | `def validate_*(self, ..., fail_value=DecodeFail.*)` | zero or more |
| Variant subclasses | nested `class Foo(Variant)` | zero or more |
| JSON override | `def to_json(self) -> list[JsonRecord]` | optional |

### 1.1 Dispatchers

A *dispatcher* is a decoder class that, instead of declaring fields and a
pipeline directly, defines a `dispatch(self) -> tuple` method returning a
tuple of decoder instances.  At decode time, the dispatcher calls each
delegate in turn and returns on the first success.

```python
class current_cost(Decoder):
    class modulation_config(ModulationConfig):
        device_name = "CurrentCost Current Sensor"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 250
        long_width = 250
        reset_limit = 8000

    def dispatch(self):
        return (current_cost_envir(), current_cost_classic())
```

The compiler generates one decode function per delegate (compiled as if they
were standalone decoders) and a dispatcher decode function:

```c
static int current_cost_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_EARLY;
    ret = current_cost_envir_decode(decoder, bitbuffer);
    if (ret > 0) return ret;
    /* undo A's in-place pipeline mutations before trying B */
    ret = current_cost_classic_decode(decoder, bitbuffer);
    if (ret > 0) return ret;
    return ret;
}
```

Between delegate calls, the compiler replays the inverse of any in-place
mutations (currently `invert` and `reflect`) from the previous delegate's
pipeline, stopping at the first `manchester_decode` step.

The `r_device` struct uses the dispatcher's own `modulation_config`.

---

## 2. ModulationConfig

A `ModulationConfig` subclass named `modulation_config` carries the radio
parameters for the `r_device` struct.  Every decoder (including dispatchers)
must supply one.

| Python attribute | C field | Type | Required |
|---|---|---|---|
| `device_name` | `.name` | string | yes |
| `modulation` | `.modulation` | `Modulation.*` enum | yes |
| `short_width` | `.short_width` | float | yes |
| `long_width` | `.long_width` | float | yes |
| `reset_limit` | `.reset_limit` | float | yes |
| `gap_limit` | `.gap_limit` | float | no |
| `sync_width` | `.sync_width` | float | no |
| `tolerance` | `.tolerance` | float | no |
| `disabled` | `.disabled` | int | no |

`device_name` is used as the default value of the `"model"` JSON key in
the default `to_json`.  To use a different model string, override `to_json`
and supply the desired string literal directly.

---

## 3. The pipeline (`prepare()`)

`prepare()` declares the bitbuffer preprocessing steps.  Its signature is:

```python
def prepare(self, buf: BitBuffer) -> BitBuffer:
```

The return value is the *tip* of the pipeline — the bitbuffer that field
extraction will read from.  Each pipeline step takes the current `BitBuffer`
and returns a (possibly new) one.  Steps that operate in-place (e.g.
`invert`, `reflect`) return the same buffer object; steps that produce a new
buffer from part of the input (e.g. `manchester_decode`) return that new
buffer.

The tip tracks a **row** index (initially `0`) and a **bit offset** (initially
`0`).  Both are updated by pipeline steps.

Because field extraction always reads from the tip, there is no need for the
code generator to track "am I after a Manchester decode?" — it always uses the
tip.

```python
def prepare(self, buf: BitBuffer) -> BitBuffer:
    return (buf
        .invert()
        .search_preamble(0xAAA9, bit_length=16, scan_all_rows=True)
        .skip_bits(16)
        .manchester_decode(160))
```

### 3.1 Pipeline steps

The generated C for each step is shown below.  In pipeline step code,
`bitbuffer` refers to the multi-row `bitbuffer_t *` that the step operates on;
`b` is reserved for the single-row `uint8_t *` used during field extraction
(§4) and does not appear in pipeline code.

#### `invert()`

```c
bitbuffer_invert(bitbuffer);
```

Inverts all bits of all rows in-place.  Returns the same buffer.

#### `reflect()`

```c
for (int i = 0; i < bitbuffer->num_rows; ++i)
    reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);
```

Bit-reverses every byte in every row, in-place.  Returns the same buffer.
After `manchester_decode` the tip has only one row, so this degenerates to a
single `reflect_bytes` call on `bitbuffer->bb[0]`.

#### `find_repeated_row(min_repeats, min_bits, max_bits=None)`

```c
int row = bitbuffer_find_repeated_row(bitbuffer, min_repeats, min_bits);
if (row < 0)
    return DECODE_ABORT_EARLY;
if (bitbuffer->bits_per_row[row] != min_bits)   // when max_bits is None
    return DECODE_ABORT_LENGTH;
if (bitbuffer->bits_per_row[row] > max_bits)    // when max_bits is given
    return DECODE_ABORT_LENGTH;
```

Sets the tip's `row` to the found row.  When `max_bits` is `None`, requires
an exact length match; when `max_bits` is given, requires `<= max_bits`.

#### `search_preamble(pattern, bit_length=None, scan_all_rows=False)`

Searches for the byte pattern `pattern` (a Python integer, big-endian) of
length `bit_length` bits.  `bit_length` defaults to `pattern.bit_length()`
(a Python compile-time constant — no C code is generated for this).

**Single-row (default, `scan_all_rows=False`):**
```c
uint8_t const preamble[] = { /* big-endian bytes of pattern */ };
if (bitbuffer->num_rows < 1)
    return DECODE_ABORT_LENGTH;
unsigned offset = bitbuffer_search(bitbuffer, row, 0, preamble, bit_length);
if (offset >= bitbuffer->bits_per_row[row])
    return DECODE_ABORT_EARLY;
```

Updates the tip's `offset`.

**All-rows (`scan_all_rows=True`):**
```c
uint8_t const preamble[] = { /* … */ };
if (bitbuffer->num_rows < 1)
    return DECODE_ABORT_LENGTH;
int preamble_found = 0;
for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {
    unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, bit_length);
    if (pos < bitbuffer->bits_per_row[row]) {
        tip_row = row;
        offset = pos;
        preamble_found = 1;
    }
}
if (!preamble_found)
    return DECODE_ABORT_EARLY;
```

Updates both `row` and `offset` on the tip.

#### `skip_bits(n)`

```c
offset += n;              // n > 0
```
```c
if (offset < abs(n))
    return DECODE_FAIL_SANITY;
offset -= abs(n);         // n < 0
```

Adjusts the tip's `offset`.

#### `manchester_decode(max_bits=0)`

```c
bitbuffer_t packet_bits = {0};
bitbuffer_manchester_decode(bitbuffer, row, offset, &packet_bits, max_bits);
if (packet_bits.bits_per_row[0] < 8)
    return DECODE_ABORT_LENGTH;
```

Decodes Manchester-encoded bits from `(row, offset)` of the current tip into
a newly declared local `bitbuffer_t packet_bits`.  The tip is then rebound to
`&packet_bits` (row 0, offset 0): all subsequent pipeline steps and field
extractions use `packet_bits` as `bitbuffer` and row `0`.

`max_bits = 0` means no limit.

---

## 4. Field declarations

Fields are Python class-level annotations, collected in **declaration order**
(base-class fields before derived-class fields; a derived class can shadow a
base field by redeclaring the same name).

After the pipeline, the decode function declares a row byte pointer:

```c
uint8_t *b = bitbuffer->bb[row];
```

`b` is always an alias for the row pointer of the tip bitbuffer at the current
row; no separate byte-array copy is made.

All field extraction uses a running `bit_pos` counter, initialized to the
tip's current offset after the pipeline.  The C compiler constant-folds
`bit_pos` to a literal whenever no `Repeat[]` field has been seen yet.

### 4.1 Private fields

A field whose name begins with `_` is *private*: its bits are consumed (the
offset advances) but no C variable is declared and no extraction is performed.

```c
bit_pos += width;
```

### 4.2 `Bits[n]`

Extract `n` bits as an unsigned integer.

```c
int name = bitrow_get_bits(b, bit_pos, n);
bit_pos += n;
```

### 4.3 `Literal[value, n]`

Extract `n` bits and assert they equal `value`.

```c
if (bitrow_get_bits(b, bit_pos, n) != 0xVALUE)
    return DECODE_FAIL_SANITY;
bit_pos += n;
```

No C variable is declared.

### 4.4 `Repeat[count_expr, sub: Repeatable]`

Parse the `Repeatable` sub-protocol layout `count_expr` times into parallel
arrays.

```c
int name_subfield1[count_expr];
int name_subfield2[count_expr];
/* … one array per non-private Bits[] sub-field */
for (int _i = 0; _i < count_expr; _i++) {
    name_subfield1[_i] = bitrow_get_bits(b, bit_pos, w1);
    bit_pos += w1;
    name_subfield2[_i] = bitrow_get_bits(b, bit_pos, w2);
    bit_pos += w2;
}
```

`count_expr` may be an integer literal or a DSL expression referencing a
previously parsed field.

The sub-protocol must be a `Repeatable` subclass (see §4.6), which enforces
that it contains only `Bits[]` and `Literal[]` fields.

There is no implicit cap on iteration count.  If the protocol needs a maximum,
the user should clamp `count_expr` explicitly.

### 4.5 `Rows[row_spec, sub: Repeatable, required_bits=()]`

Parse the `Repeatable` sub-protocol layout once per selected bitbuffer row,
storing results in sparse arrays indexed by row number.

`row_spec` is one of:
- A tuple of integer row indices `(row1, row2, ...)` — parse exactly those rows.
- A `FirstValid(bits)` object — scan all rows and use the first one that passes
  validation (see §4.5.1).

```c
int name_subfield1[BITBUF_ROWS];
int name_subfield2[BITBUF_ROWS];

static uint16_t const _rows_name[] = {row1, row2, ...};
for (size_t _k = 0; _k < nrows; ++_k) {
    unsigned _r = _rows_name[_k];
    uint8_t *b = bitbuffer->bb[_r];
    unsigned bit_pos = 0;
    name_subfield1[_r] = bitrow_get_bits(b, bit_pos, w1);
    bit_pos += w1;
    name_subfield2[_r] = bitrow_get_bits(b, bit_pos, w2);
    bit_pos += w2;
}
```

Array elements are indexed by the *row number*, not the loop counter, so
`name_subfield1[2]` means "subfield1 as read from bitbuffer row 2".  Slots
for rows not in `row_spec` are uninitialized.

The sub-protocol must be a `Repeatable` (see §4.6).

The optional `required_bits` argument is a tuple of `(row_index, exact_bits)`
pairs.  For each pair, the compiler emits a length guard at the point where the
`Rows[]` field appears:

```c
if (bitbuffer->bits_per_row[row_index] != exact_bits)
    return DECODE_ABORT_LENGTH;
```

#### 4.5.1 `FirstValid` row selection

When `row_spec` is `FirstValid(bits)`, the generated code scans all rows and
uses the first one whose length matches.  Unlike the static-row-tuple form,
`FirstValid` wraps the *entire decode body* — all subsequent fields, method
calls, validation hooks, and `data_make` — inside a row-scanning loop:

```c
int result = 0;
for (int _r = 0; _r < bitbuffer->num_rows; ++_r) {
    if (bitbuffer->bits_per_row[_r] != bits) {
        result = DECODE_ABORT_LENGTH;
        continue;
    }
    uint8_t *b = bitbuffer->bb[_r];
    unsigned bit_pos = 0;
    /* sub-protocol fields, then all decoder fields, methods,
       validation hooks, and data_make follow here */
    /* return 1 on success */
}
return result;
```

If no row passes, the function returns the last recorded failure code.

**Validation hooks inside `FirstValid`:** `validate_*` hooks that fire inside a
`FirstValid` loop use `continue` instead of `return` on failure — they skip to
the next candidate row rather than aborting the entire decode:

```c
if (!prefix_validate_checksum(sensor_id, checksum)) {
    result = DECODE_FAIL_MIC;
    continue;
}
```

Hooks that fire *outside* a `FirstValid` loop retain normal `return` behavior.
The code generator tracks whether it is currently inside a `FirstValid` loop
and emits the appropriate control flow.

### 4.6 `Repeatable`

`Repeatable` is a subclass of `Protocol` that enforces a restriction: its
fields may only be `Bits[]` or `Literal[]`.  `Repeat[]` and `Rows[]` are
not allowed inside a `Repeatable`.  The DSL enforces this at class-definition
time.

`Variant` (§6) inherits from `Repeatable`, so variant bodies are subject to
the same restriction.

---

## 5. Callables

Callables are methods defined in the decoder (or variant) class body.  Every
callable must have a return type annotation.

The compiler classifies each callable by its name:

| Name pattern | Class | C output |
|---|---|---|
| `validate_*` | *validation hook* | `static inline` C function + auto-called guard |
| anything else | *method* | `static inline` C function + called at each use site |

Reserved names not compiled as callables: `prepare`, `to_json`, `when`,
`dispatch`.

All callables are *pure*: they take named inputs and return a value with no
side effects.  The compiler emits each callable as a `static inline` C function
defined before the decode function.  Methods are called directly at each use
site (in expressions passed to `data_make`, or as arguments to other
methods); no intermediate local variable is introduced for a method's result.

### 5.1 Inline vs. external callables

A callable is *inline* if its Python body is a single `return expr` where
`expr` is a DSL expression (built from references to parameters, operators,
`Rev8`, `LfsrDigest8`, and literals).  The AST extraction happens inside
`dsl.py`: when the method is defined, `dsl.py` calls it with `FieldRef` proxy
arguments and stores the resulting expression tree as an annotation on the
method object.  The code generator (`codegen.py`) reads that annotation to
emit the C function body.

A callable is *external* if its body is `...` (Ellipsis).  The compiler
emits only a `static inline` declaration; the user provides the definition in a
companion `.h` file.  External callables must be declared `static inline` in C so
that they may be called freely at any use site without caching concerns.

### 5.2 Argument inference

For external callables, arguments are taken directly from the declared
parameter names (the compiler does not infer them).

For inline callables, all `FieldRef`s in the expression body are collected
automatically by `dsl.py`.  The emitted C function takes exactly those names
as parameters.  At each call site the compiler passes the corresponding
field locals or the results of other `static inline` function calls.  The
compiler verifies that all referenced names are in scope at every call site.

A callable may reference the result of another callable; because all callables
are `static inline` functions called at use sites, there is no ordering constraint
between them beyond what the field parse order dictates.

### 5.3 Methods

A *method* is any non-`validate_*` callable.

```python
def temperature_c(self, temp_raw: int) -> float:
    return (temp_raw - 500) * 0.1
```

Emits (before the decode function):

```c
static inline float temperature_c(int temp_raw) {
    return ((temp_raw - 0x1f4) * 0.1);
}
```

In `data_make` (or anywhere else the result is needed), the function is called
directly:

```c
data_make(
    "temperature_c", "", DATA_DOUBLE, (double)temperature_c(temp_raw),
    NULL);
```

No intermediate local variable is introduced for `temperature_c`.

Return type annotation controls the C type:

| Python annotation | C type | `data_make` type |
|---|---|---|
| `int` (or anything else) | `int` | `DATA_INT` |
| `float` | `float` | `DATA_DOUBLE` |
| `str` | `const char *` | `DATA_STRING` |

External method (body is `...`):

```python
def crc8(self, b: bytes, n: int) -> int: ...
```

Emits only a declaration; the user provides a `static inline` C definition in the
companion header.

### 5.4 Emission ordering

Method *bodies* (C function definitions) are emitted before the decode
function and have no ordering constraint relative to each other.

Method *calls* are emitted at their use sites — wherever the method's result
appears in an expression (e.g., as a `data_make` argument or as an argument to
another method).  There is no separate "call phase" for regular methods.

Validation hooks are the exception: their calls are emitted *automatically* by
the code generator as soon as all of the hook's field dependencies have been
parsed, regardless of where they appear in `to_json` or elsewhere.

### 5.5 Validation hooks (`validate_*`)

A validation hook is a callable whose name starts with `validate_`.  Like
other methods, its body is compiled to a `static inline` C function.  Unlike other
methods, its call is emitted *automatically* by the code generator as soon as
all of its dependencies (declared parameters, excluding `fail_value`) have been
parsed.

Every validation hook's last parameter must be `fail_value=DecodeFail.*`
with a default value from the `DecodeFail` enum.  The hook returns `bool`
(non-zero means the packet is OK).  `fail_value` is a compile-time constant
and is **not** passed in the C call.

```python
def validate_checksum(self, sensor_id: int, checksum: int,
                      fail_value=DecodeFail.MIC) -> bool:
    return ((sensor_id & 0xFF) + checksum) & 0xFF == 0
```

Emits (before the decode function):

```c
static inline bool prefix_validate_checksum(int sensor_id, int checksum) {
    return (((sensor_id & 0xff) + checksum) & 0xff) == 0;
}
```

**Outside a `FirstValid` loop** — normal `return` on failure:

```c
if (!prefix_validate_checksum(sensor_id, checksum))
    return DECODE_FAIL_MIC;
```

**Inside a `FirstValid` loop** — `continue` on failure:

```c
if (!prefix_validate_checksum(sensor_id, checksum)) {
    result = DECODE_FAIL_MIC;
    continue;
}
```

The code generator tracks whether it is currently emitting inside a
`FirstValid` loop and selects the appropriate control flow.

Multiple `validate_*` hooks are called in **lexicographic** name order
relative to when their dependencies are satisfied.  Choose names accordingly
when order matters, e.g. `validate_length` before `validate_mic`.

---

## 6. Variants

A `Variant` subclass nested inside a `Decoder` class declares a
conditionally-decoded payload extension.  `Variant` inherits from
`Repeatable`, so variant bodies may only contain `Bits[]` and `Literal[]`
fields (no nested `Repeat[]` or `Rows[]`).

```python
class MyVariant(Variant):
    def when(self, field_a: int) -> bool:
        return field_a == 0

    extra_field: Bits[8]

    def my_method(self, extra_field: int) -> int:
        return extra_field * 2
```

Each variant must define `when(self, ...) -> bool`.  Its body must be an
inline DSL expression.  The declared parameters name the fields (from the
parent or the variant itself) that the predicate depends on.

### 6.1 Variant dispatch code

After the parent's fields and automatic validation hook calls are emitted, the
compiler emits an `if` / `else if` chain:

```c
if (when_expr_0) {
    /* variant 0 fields */
    /* variant 0 validation hooks and data_make */
} else if (when_expr_1) {
    /* variant 1 … */
}

return DECODE_FAIL_SANITY;  // no variant matched
```

Variants are emitted in declaration order.

### 6.2 Variant fields

Inside a variant branch, `bit_pos` continues from where the parent left off.
Variant fields follow the same rules as parent fields.

### 6.3 Variant callables

Every callable defined in a variant is prefixed with the variant's class name
in the emitted C function name: `prefix_VariantName_method_name`.

Inside the variant branch, method calls and field references resolve to the
variant's own names (shadowing the parent's when names collide).  Variant
methods may reference parent fields as well as the variant's own fields.

### 6.4 Variant `to_json`

If the variant defines `to_json`, that override is used for the variant's
`data_make`.  Otherwise, the compiler emits a default `data_make` covering
the model name (the parent's `device_name`), all non-private parent and
variant `Bits[]` fields, and all non-`validate_*` methods of both parent and
variant.  `validate_*` hooks are emitted as guard conditions elsewhere in the
decode function and never appear as JSON fields.

---

## 7. Output: `to_json` and `data_make`

### 7.1 How `to_json` is evaluated

The compiler *calls* `to_json()` at Python compile time to retrieve the list
of `JsonRecord` objects.  It does not inspect the AST of `to_json`.  Attribute
accesses on `self` inside `to_json` return `FieldRef` objects (or method
references), which the code generator later uses to emit C expressions or
function calls.

### 7.2 Default `to_json`

When no `to_json` is defined, the compiler generates a `data_make` call with:

1. `"model"` key: string value = `device_name` from `modulation_config`.  For a
   variant the parent decoder's `device_name` is used.
2. All non-private `Bits[]` fields (parent then variant), in declaration order.
3. All non-`validate_*` methods of the current class (parent or variant), in
   declaration order.  `validate_*` hooks are emitted as guard conditions and
   never appear as JSON fields.

### 7.3 Explicit `to_json`

```python
def to_json(self) -> list[JsonRecord]:
    return [
        JsonRecord("model",       "",          "Ford",             "DATA_STRING"),
        JsonRecord("temperature", "Temp",      self.temperature_c, "DATA_DOUBLE"),
        JsonRecord("mic",         "Integrity", "CHECKSUM",         "DATA_STRING"),
    ]
```

The `value` of a `JsonRecord` may be:

- A string literal → emitted verbatim in `data_make`.
- A DSL expression (`FieldRef` or compound) → emitted as a C expression.
- A method reference (`self.method_name`) → emitted as a call to the
  corresponding `static inline` C function at that point in `data_make`.

### 7.4 `JsonRecord` fields

```
JsonRecord(key, label, value, data_type, fmt=None)
```

- **`key`** — JSON key string.
- **`label`** — human-readable label (may be empty string).
- **`value`** — a string literal, a DSL expression, or a method reference.
- **`data_type`** — one of `"DATA_INT"`, `"DATA_DOUBLE"`, `"DATA_STRING"`.
- **`fmt`** — optional format string.  Emits `DATA_FORMAT, "fmt"` before the
  data type.  When `data_type` is `"DATA_STRING"`, `fmt` is not `None`, and
  `value` is not a string literal, the compiler pre-declares a `char[64]`
  buffer and calls `snprintf` to format the value into it.

For conditional field emission (include a field only when a predicate
holds), use variant dispatch: define multiple `Variant` classes whose
`when` methods gate on the predicate and whose `to_json` lists include
only the applicable fields.  `acurite_01185m` demonstrates this pattern.

### 7.5 `output_fields` consistency

The `output_fields[]` array lists every JSON key that may appear across all
`data_make` call sites (parent and all variant branches).  All variant
`to_json` overrides must draw their key names from the same declared set;
the compiler validates this.

---

## 8. Expression language

DSL expressions are built using Python operator overloading on `Expr` objects
and rendered to C at compile time by the code generator.

| Python | C |
|---|---|
| Integer literal `n` | `hex(n)` if `n > 9`, else decimal |
| Float literal `f` | `repr(f)` (Python's float repr) |
| Bool literal `True`/`False` | `1` / `0` |
| `FieldRef("name")` | `name` |
| `FieldRef("name")[i]` | `(name[i])` |
| `a + b` | `(a + b)` |
| `a - b` | `(a - b)` |
| `a * b` | `(a * b)` |
| `a / b` or `a // b` | `(a / b)` |
| `a % b` | `(a % b)` |
| `a & b` | `(a & b)` |
| `a \| b` | `(a \| b)` |
| `a ^ b` | `(a ^ b)` |
| `a << b` | `(a << b)` |
| `a >> b` | `(a >> b)` |
| `~a` | `(~a)` |
| `-a` | `(-a)` |
| `a == b` | `(a == b)` |
| `a != b` | `(a != b)` |
| `a < b` | `(a < b)` |
| `a <= b` | `(a <= b)` |
| `a > b` | `(a > b)` |
| `a >= b` | `(a >= b)` |
| `Rev8(x)` | `(reverse8(x))` |
| `LfsrDigest8(b0, b1, …, gen=g, key=k)` | `(lfsr_digest8((uint8_t const[]){b0, b1, …}, n, g, k))` |

All binary operators produce a parenthesized `(left op right)`.  Nested
expressions are fully parenthesized.

**Note:** Python's `==` on `Expr` objects returns a `BinaryExpr("==", ...)`,
not a Python `bool`.  Use `&` and `|` for logical AND/OR, not `and`/`or`.

---

## 9. Inheritance

Decoder classes use ordinary Python inheritance.  Because the compiler
operates on *instances*, Python's standard attribute lookup resolves inherited
members automatically — the compiler reads fields, callables, and variants
from the instance without any explicit MRO traversal.

Fields are declared as class-level annotations; the merged declaration order
(base before derived) is the field parse order.  A derived class shadows a
base field by redeclaring the same name.

Each concrete decoder instance is compiled independently to its own C decode
function.  Shared logic between sibling decoders is not deduplicated in the
emitted C.

---

## 10. Generated C file structure

For a decoder instance of class `foo`:

```c
// Generated from foo.py
/** @file
    Device name decoder.
*/

#include "decoder.h"
#include "foo.h"          // only if external helpers are needed

/* inline C functions for all methods and validation hooks */
static inline float temperature_c(int temp_raw) { … }
static inline bool  prefix_validate_checksum(int sensor_id, int checksum) { … }

/* delegate decode functions (if dispatcher), then: */

static int foo_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /* pipeline — operates on bitbuffer / packet_bits */
    /* b = bitbuffer->bb[row];  bit_pos = offset; */
    /* field extractions */
    /* auto-inserted validate_* calls (return or continue on failure) */
    /* variant dispatch or data_make with inline inline calls */
    /* return 1; */
}

static char const *const output_fields[] = {
    "model",
    "field1",
    /* … */
    NULL,
};

r_device const foo = {
    .name        = "Device Name",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 105.0,
    .long_width  = 105.0,
    .reset_limit = 1500.0,
    /* optional: .gap_limit, .sync_width, .tolerance, .disabled */
    .decode_fn   = &foo_decode,
    .fields      = output_fields,
};
```

### 10.1 External helper header

If any callable has a body of `...`, the compiler emits `#include "prefix.h"`.
The user must provide that header with `static inline` definitions for all external
C functions.

---

## 11. Complete decode function skeleton

```
foo_decode(decoder, bitbuffer):

  // Pipeline  (operates on bitbuffer; may rebind to packet_bits after manchester_decode)
  bitbuffer = prepare(bitbuffer)   // each step emits C; tip may be packet_bits at end
  uint8_t *b    = bitbuffer->bb[row]
  unsigned bit_pos = offset        // tip's offset after pipeline

  // Field extraction + auto validate_* calls
  //   outside FirstValid: validate failure → return DECODE_FAIL_*
  for each (name, spec) in fields:
    emit extraction for spec (Bits, Literal, Repeat, Rows)
    for each validate_* whose deps ⊆ parsed_so_far (lex order):
      emit: if (!hook(...)) return DECODE_FAIL_*;

  // FirstValid wraps the above in a row loop; inside the loop:
  //   validate failure → result = DECODE_FAIL_*; continue

  // Variant dispatch (if variants exist)
  if (when_expr_0) {
    emit variant_0 fields
    for each validate_* whose deps ⊆ parsed_so_far: emit guard (return or continue)
    emit data_make with inline calls at use sites
    return 1
  } else if (when_expr_1) { … }
  return DECODE_FAIL_SANITY

  // Non-variant path
  for each remaining validate_* whose deps now satisfied: emit guard
  data_make(… inline calls inline …)
  decoder_output_data(decoder, data)
  return 1
```
