# Compiler Bugs

Previously tracked issues (2, 3, 7, 13) have been resolved.


## Bug 14 — Validate hooks with unsatisfiable deps silently never fire

**Status: Fixed.**

**Resolution:** Two fixes:

1. Renamed `_mid` → `mid` and `_checksum` → `checksum` in `acurite_01185m_row`
   so `validate_checksum`'s deps are satisfiable.

2. Added `_resolve_callable_deps()` fixpoint loop in `emit_decode_fn`: after
   all fields are extracted, callable names whose own deps are satisfied are
   added to `parsed_fields`. This lets validate hooks depend on callable
   results (e.g. `akhan_100F14.validate_cmd` depends on `cmd` which depends
   on `notb2` which depends on field `b2`).

3. Added a compile-time error at the end of `emit_decode_fn`: any validate
   hook whose deps are still unsatisfied after the fixpoint raises `TypeError`.
   This prevents silent omission of validation.


---

## Bug 15 — Default variant `to_json` ignores parent fields and methods (SEMANTICS.md 6.4 / 7.2)

**Status: Open. Severity: Low. Component: `codegen.py` `_emit_variants` + `dsl.py` `_default_json_records`.**

**Spec rule violated:** SEMANTICS.md 6.4 says: "the compiler emits a default `data_make`
covering the model name, all non-private parent and variant `Bits[]` fields, and all
variant methods." SEMANTICS.md 7.2 says: "All methods of the current class."

Two issues combine:

1. `_default_variant_records` in `codegen.py` (lines 1041-1057) correctly includes parent
   and variant `Bits[]` fields but **omits all methods/props** from both parent and variant.

2. `_default_variant_records` is **dead code**: at line 1031, `vcls.json_records()` always
   returns a non-empty tuple (via `_default_json_records` which always produces at least a
   "model" record), so the `or _default_variant_records(...)` branch is unreachable.
   The actual fallback goes through `dsl.py`'s `_default_json_records` on the variant class,
   which only includes the variant's own fields -- not parent fields -- and uses the variant
   class name as model instead of `device_name` from `modulation_config`.

**Practical impact:** Currently none -- all protocol variants define explicit `to_json`.
Would produce wrong output if a variant omitted `to_json` in the future.

**Fix:** Either (a) make `_default_json_records` variant-aware (accept optional parent class),
or (b) fix `_emit_variants` to bypass `json_records()` for variants without explicit `to_json`.

---

## Bug 16 — Default `to_json` excludes callables without return type annotations

**Status: Open. Severity: Low. Component: `dsl.py` `_default_json_records`.**

**Spec rule violated:** SEMANTICS.md 5.3 defines "method" as "any non-`validate_*` callable."
SEMANTICS.md 7.2 says the default `to_json` includes "All methods of the current class
(parent or variant), in declaration order."

`_default_json_records` (dsl.py line 995-1000) filters on `entry.kind == "prop"`, which
only matches callables that have an explicit return type annotation. Callables without a
return type annotation get `kind = "method"` and are excluded from the default JSON output.
The spec makes no distinction -- all non-validate callables should be included.

**Practical impact:** Currently none -- all protocols define explicit `to_json`. Would
silently omit callable results if a protocol used default `to_json` with unannotated
methods.
