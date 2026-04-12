# Plan: Remove `cls`/`type()` introspection from codegen.py

codegen.py reaches through instances to their class hierarchy via `type(instance)` and
then accesses `cls._fields`, `cls._callables`, `cls._variants`, `cls.__name__`, and
`cls.requires_external_helpers()`.  This violates the contract that dsl.py is the
validation and introspection layer: codegen should ask instances for their data, not
poke into the class definition.

The fix is not just wrapping the class attributes in instance properties — it's moving
the *computation* out of `__init_subclass__` entirely.  Python's MRO, `__annotations__`,
and `__dict__` already encode all the information; we should derive it lazily from those
built-in mechanisms instead of caching it eagerly in class-level attributes.

## 1. Inventory of `cls`/`type()` accesses in codegen.py

### 1a. `_decoder_name(instance)` — line 234
```python
return type(instance).__name__
```
Used by `emit_dispatcher` and `_compile_dispatcher`.  Extracts the protocol name.

### 1b. `_get_callables(cls)` — line 243
```python
return list(cls._callables.items())
```
Called from `_compile_single_decoder` and `_compile_dispatcher` to iterate callables
of a class (not an instance).

### 1c. `emit_decode_fn` — line 846
```python
cls = type(instance)
...
callables = dict(cls._callables)
fields = list(cls._fields)
...
variants = list(cls._variants)
...
records = list(cls.json_records())
```
The big one — needs fields, callables, variants, and json_records from the class.

### 1d. `_emit_variants` — line 1024
```python
# Receives `cls: type` directly
vcls.when              # the when() function
vcls._callables
vcls._fields
vcls.__name__
vcls.json_records()
```
Iterates variant subclasses and accesses their fields/callables/name.

### 1e. `_default_variant_records` — line 1076
```python
parent_cls.__new__(parent_cls).modulation_config().device_name
parent_cls._fields
vcls._fields
```
Synthesizes default JsonRecords by inspecting parent and variant fields.

### 1f. `emit_output_fields` — line 1142
```python
cls = type(src)
cls.json_records()
cls._variants   # then vcls.json_records()
```
Unions output keys from a class and its variants.

### 1g. `_emit_file_header` — line 1195
```python
type(instance).__name__
```
Gets the decoder name for the file comment.

### 1h. `_compile_single_decoder` — line 1208
```python
cls = type(instance)
cls.__name__
cls.requires_external_helpers()
cls._variants  # iterate variant callables
```

### 1i. `_compile_dispatcher` — line 1243
```python
cls = type(instance)
cls.__name__
type(d).requires_external_helpers()   # for each delegate
type(delegate).__name__
_get_callables(type(delegate))
```

## 2. What codegen actually needs

Across all these sites, codegen needs exactly six things from a protocol instance:

| Need                  | Current access             | Used in                                      |
|-----------------------|---------------------------|----------------------------------------------|
| Protocol name         | `type(inst).__name__`     | everywhere                                   |
| Fields list           | `cls._fields`             | `emit_decode_fn`, `_emit_variants`, `_default_variant_records` |
| Callables dict        | `cls._callables`          | `emit_decode_fn`, `_compile_single_decoder`, `_emit_variants`, `_compile_dispatcher` |
| Variants list         | `cls._variants`           | `emit_decode_fn`, `_compile_single_decoder`, `emit_output_fields` |
| Json records          | `cls.json_records()`      | `emit_decode_fn`, `_emit_variants`, `emit_output_fields` |
| Has external helpers  | `cls.requires_external_helpers()` | `_compile_single_decoder`, `_compile_dispatcher` |

## 3. Proposed instance properties — computed from Python built-ins, not class attrs

The goal is to delete `cls._fields`, `cls._callables`, `cls._variants` and the
`__init_subclass__` code that populates them.  Each property computes its value
from Python's MRO, `__annotations__`, and `__dict__` — the same built-in machinery
that `__init_subclass__` was manually reimplementing.

### 3a. `name` — property
```python
@property
def name(self) -> str:
    return type(self).__name__
```
Standard Python.  The only `type(self)` call in the entire DSL.

### 3b. `fields` — property, computed from MRO + `__annotations__`
```python
@property
def fields(self) -> tuple[tuple[str, Any], ...]:
    merged: dict[str, Any] = {}
    for cls in reversed(type(self).__mro__):
        for name, ann in cls.__dict__.get("__annotations__", {}).items():
            if isinstance(ann, (BitsSpec, LiteralSpec, RepeatSpec, RowsSpec)):
                merged[name] = ann
    return tuple(merged.items())
```
Walks the MRO in base-first order (reversed because `__mro__` is derived-first).
This is what `__init_subclass__` does today, except it eagerly caches the result
in `cls._fields`.  Python's MRO gives us the ordering for free — no manual
inheritance bookkeeping needed.

### 3c. `callables` — property, computed from MRO + `__dict__`
```python
@property
def callables(self) -> dict[str, CallableEntry]:
    merged: dict[str, CallableEntry] = {}
    for cls in reversed(type(self).__mro__):
        for attr_name, attr_val in cls.__dict__.items():
            if not _is_user_callable(attr_name, attr_val):
                continue
            merged[attr_name] = _build_callable_entry(cls, attr_name, attr_val)
    return merged
```
Same MRO walk.  `_build_callable_entry` is the existing callable-processing logic
from `__init_subclass__` step 3, extracted into a function (it already calls
`_call_inline`, `_infer_param_type`, `_fail_value_default`, etc.).

One wrinkle: `_infer_param_type(cls, p)` currently takes the class.  Since
we're iterating MRO classes, we have `cls` available in the loop — but
`_infer_param_type` needs to see *all* fields (including inherited ones) to
resolve a parameter name to a type.  So it should take the instance (and use
`self.fields`) or accept the already-computed fields.

<!-- Q: should _infer_param_type take the instance? Or take a fields tuple? -->

Performance: `_call_inline` parses AST + calls the function with proxies.  This
runs every time `.callables` is accessed.  Options:
1. `functools.cached_property` — caches per-instance, fine since instances are
   short-lived (created for compilation, then discarded).
2. Accept the recomputation — it's cheap relative to C code generation.

Recommend `functools.cached_property` for all three computed properties.

### 3d. `variants` — property, computed from `__dict__`
```python
@property
def variants(self) -> tuple:  # tuple of Variant instances
    result = []
    for cls in reversed(type(self).__mro__):
        for attr_name, attr_val in cls.__dict__.items():
            if (
                isinstance(attr_val, type)
                and issubclass(attr_val, Variant)
                and attr_val is not Variant
            ):
                result.append(attr_val())  # instantiate
    return tuple(result)
```
Returns **instances**, not classes.  This means codegen can use the same
`v.name`, `v.fields`, `v.callables` interface as any Protocol — no special
variant-class-introspection path needed.

`_call_inline(v.when)` works on bound methods because `_call_inline` calls
`inspect.signature(fn)`, which handles bound methods by excluding `self`
from the signature.  The proxy `_FieldRefProxy()` is passed explicitly as
the first positional arg.  No change needed to `_call_inline`.

### 3e. `json_records()` — already a classmethod, works on instances
Already callable as `instance.json_records()`.  Its internals reference
`cls._fields` and `cls._callables` — after this refactor, it should use
`self.fields` and `self.callables` instead, which means converting it from
a `@classmethod` to a regular instance method.

### 3f. `requires_external_helpers()` — convert from classmethod to instance method
Currently walks `cls._callables` and `cls._variants`.  After refactor, uses
`self.callables` and `self.variants`.

## 4. What stays in `__init_subclass__`

Validation that must fail at class-definition time (not at compile time):

- Variant subclasses must define `when()` with a compilable body
- Repeatable subclasses must not contain Repeat/Rows fields
- Decoder subclasses must define `modulation_config` and `prepare`

These are pure validation checks — they don't produce data, they just raise on
bad definitions.  Keep them in `__init_subclass__`.

Everything that *produces data* (`_fields`, `_callables`, `_variants`) moves to
instance properties.

## 5. Execution plan

### Step 1: Extract `_build_callable_entry` from `__init_subclass__`
Factor the callable-processing logic (lines 878–930 of current dsl.py) into a
standalone function `_build_callable_entry(cls, attr_name, attr_val) -> CallableEntry`.
This is a pure refactor — `__init_subclass__` still calls it, behavior unchanged.

### Step 2: Add computed instance properties to Protocol
Add `name`, `fields`, `callables`, `variants` as `@functools.cached_property`.
These compute from MRO/`__annotations__`/`__dict__` as described in section 3.
Tests should still pass since both the old class attributes and new instance
properties coexist.

### Step 3: Convert `json_records()` and `requires_external_helpers()` to instance methods
Change from `@classmethod` to regular methods.  Update their bodies to use
`self.fields`, `self.callables`, `self.variants` instead of `cls._fields` etc.

### Step 4: Update codegen.py — replace all `type(instance)` and `cls` accesses
Go function by function:

- **`_decoder_name(instance)`**: replace body with `return instance.name`
- **`_get_callables(cls)`**: delete entirely.  Callers use `instance.callables`.
- **`emit_decode_fn`**: delete `cls = type(instance)`.  Use `instance.fields`,
  `instance.callables`, `instance.variants`, `instance.json_records()`.
- **`_emit_variants`**: change to accept variant instances.  Use `v.name`,
  `v.fields`, `v.callables`, `v.json_records()`.
- **`_default_variant_records`**: take parent instance + variant instance.
  Use `parent.modulation_config()`, `parent.fields`, `v.fields`.
- **`emit_output_fields`**: use `src.json_records()`, `src.variants`.
- **`_emit_file_header`**: use `instance.name`.
- **`_compile_single_decoder`**: use `instance.name`, `instance.requires_external_helpers()`,
  `instance.callables`, `instance.variants`.
- **`_compile_dispatcher`**: use `instance.name`, delegate instances directly.

### Step 5: Delete class-attribute population from `__init_subclass__`
Remove the `cls._fields = ...`, `cls._variants = ...`, `cls._callables = ...`
assignments and their preceding computation.  Keep only the validation checks.

Delete the class-level defaults (`_fields = ()`, `_variants = ()`, `_callables = {}`).

### Step 6: Run tests, grep for stragglers
```
python3 -m pytest proto_compiler/test/
```
Grep codegen.py for any remaining `type(`, `cls.`, `cls[`, `__class__`.
