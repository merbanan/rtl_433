# Compiler Bugs

Previously tracked issues (2, 3, 7, 13) have been resolved.

**Status as of 2026-04-12:** all bugs below are resolved. The rtl_433_tests
end-to-end suite runs green (1827/1827) with the full set of fixes applied;
`python3 -m pytest proto_compiler/test/` reports 93 passed.

## Summary

| # | Status | Title | Severity | Component |
|---|---|---|---|---|
| 15 | Fixed | Variant default `to_json` uses variant class name as model and omits parent fields/methods | — | `dsl.py` `_default_json_records`, `codegen.py` `_emit_variants` |
| 16 | Fixed | Callables without return type annotations are silently accepted | — | `dsl.py` callable validation |
| 19 | Fixed | `when` method references in DATA_COND emitted as bare names | — | `acurite_01185m.py`, `dsl.py`, `codegen.py`, `SEMANTICS.md`, `README.md` |
| 22 | Fixed | Dispatcher delegate variant methods not emitted | — | `codegen.py` `_compile_dispatcher` |
| 21 | Fixed | `scan_all_rows` preamble search never declares `offset` | — | `codegen.py` `emit_pipeline` |
| 20 | Fixed | Callable arguments not expanded when referencing other callables | — | `codegen.py` `_expand_callable_args`, `_value_to_c` |
| 18 | Fixed | FirstValid field references not indexed inside row loop | — | `codegen.py` |
| 17 | Fixed | `static constexpr` in function definitions is not valid C | — | `codegen.py`, `CMakeLists.txt`, `SEMANTICS.md` |
| 14 | Fixed | Validate hooks with unsatisfiable deps silently never fire | — | `codegen.py`, `acurite_01185m.py` |

---

# Resolved bugs

## Bug 15 — Variant default `to_json` uses variant class name as model and omits parent fields/methods

**Status: Fixed.**

**Spec rule violated:** SEMANTICS.md §6.4 and §7.2 require the synthesized default
`data_make` for a variant to use the parent's `device_name` as the model, and to
include non-private parent fields and non-`validate_*` methods alongside the
variant's own (in parent-then-variant order). Clarified the spec text in both
sections to spell out that the "methods" lists exclude `validate_*` hooks (those
are emitted as guard conditions elsewhere).

**Resolution:** Three coordinated changes:

1. **Variant → parent back-link (`dsl.py`).** In `Protocol.__init_subclass__`,
   each registered inner-class variant is tagged with `vcls._parent_decoder = cls`
   so synthesis can reach the parent's `modulation_config`, fields, and
   callables.

2. **Variant-aware synthesis (`dsl.py` `_default_json_records`).** When
   `_parent_decoder` is set, the synthesizer pulls the model string from the
   parent's `modulation_config().device_name`, then emits parent fields/methods
   before the variant's own. Duplicate names (same key on parent and variant)
   are de-duplicated via a `seen` set.

3. **Parent-vs-variant prefix disambiguation at call sites
   (`codegen.py`).** The synthesized records reference parent methods via
   `FieldRef`, but the emission path inside a variant branch previously
   force-applied the variant's C-name prefix, producing calls to a function
   that was only emitted with the parent's prefix (link error). Added a
   `variant_owned_names` threading through `emit_data_make`, `_value_to_c`,
   `_expand_callable_args`, `emit_validate_call`, and `_emit_pending_validates`.
   Call sites now apply the variant prefix only to names in that set
   (inherited-into-variant callables); names outside it (true parent
   callables) emit with no variant prefix.

**Regression tests:** `TestDefaultJsonVariant` in
`proto_compiler/test/test_codegen.py` now pins the spec-conformant behaviour with
six assertions:

- `test_model_is_parent_device_name` — model = parent's `device_name`.
- `test_parent_field_present` — parent `Bits[]` field appears in variant's
  `data_make`.
- `test_parent_method_present` — parent method call appears with the parent's
  C-name prefix.
- `test_variant_field_present` — variant's own `Bits[]` field appears.
- `test_variant_method_present` — variant's own method call appears with the
  variant's C-name prefix.
- `test_parent_method_not_variant_prefixed` — negative check catching the
  prefix-mismatch linker bug directly.

**Date discovered:** 2026-04-12

---

## Bug 16 — Callables without return type annotations are silently accepted

**Status: Fixed.**

**Spec rule violated:** SEMANTICS.md §5 line 432: "Every callable must have a return
type annotation."

**Resolution:** Replaced the permissive `else` branch in `dsl.py`'s callable
collection with an explicit `raise TypeError` that names the offending attribute and
cites SEMANTICS.md §5. The check runs before the `validate_*` vs method split, so
`validate_*` hooks must now also annotate `-> bool` explicitly. Collapsed the now-
vestigial `"prop"` kind name to `"method"` throughout (matching spec terminology):
the `CallableEntry.kind` comment, the `_default_json_records` filter
(`dsl.py:961`), and the `_value_to_c` check (`codegen.py:634`) were updated.

The synthesized-JSON filter symptom described earlier (dropping untyped methods)
disappears automatically — the DSL now forbids the untyped callable at
class-definition time, so there's no `kind == "method"` entry that could slip
past the filter.

No shipped protocol needed changes; one test fixture (`sensor_with_validate.
validate_checksum`) was missing `-> bool` and was corrected.

**Regression tests:** `TestCallableRequiresReturnType` in
`proto_compiler/test/test_codegen.py`:

- `test_missing_return_type_raises` — a method without `->` raises `TypeError`.
- `test_missing_return_type_on_validate_raises` — a `validate_*` hook without
  `-> bool` also raises `TypeError`.

---

## Bug 19 — `when` method references in DATA_COND emitted as bare names

**Status: Fixed.**

**Resolution:** Two steps:

1. *Protocol side.* Restructured `acurite_01185m.py` to use four `Variant` subclasses
   (`BothProbes`, `MeatOnly`, `AmbientOnly`, `NoProbes`) that dispatch on the
   temperature raw-value ranges, replacing the `when=self.temp1_ok` /
   `when=self.temp2_ok` conditions. Each variant's `to_json` only includes the
   temperature fields for the probes that are live, reproducing the original
   `DATA_COND` behaviour within the DSL semantics. The four variants share helper
   methods via a module-level `_AcuriteProbeVariant` abstract base (with an
   always-false `when`) so the per-probe formulas aren't duplicated.

2. *DSL/codegen cleanup.* Removed the `when: Any = None` field from `JsonRecord` in
   `dsl.py` and the `DATA_COND` branch from `emit_data_make` in `codegen.py`.
   Updated `SEMANTICS.md` §7.4 to drop the `when=` parameter and point readers to
   variant dispatch for conditional fields. Updated `README.md` similarly.

Since no protocol uses `when=`, this removal is safe; any future attempt will now
error at `JsonRecord` construction time (unexpected keyword argument) rather than
silently emitting broken C.

**Date discovered:** 2026-04-12

---

## Bug 22 — Dispatcher delegate variant methods not emitted

**Status: Fixed.**

**Resolution:** `_compile_dispatcher` (in `codegen.py`) emitted `emit_callable` only
for each delegate's parent-level `_callables`, skipping callables defined inside the
delegate's variants. A dispatcher like `current_cost` (with `meter`/`counter` variants
defining `power0_W`, `power1_W`, `power2_W`) generated C call sites referencing
`current_cost_envir_meter_power0_W` etc. without ever defining them. Fixed by
iterating each delegate's `_variants` and emitting `emit_callable(d_name, c_name,
entry, vcls.__name__)` for every variant callable, matching the single-decoder path.
Regression test: `TestDispatcherVariantMethodsEmitted`.

**Why this wasn't caught earlier:** Bug 20 blocked `akhan_100F14.c` from compiling
first, masking this downstream issue. Fixing Bug 20 surfaced it.

---

## Bug 21 — `scan_all_rows` preamble search never declares `offset`

**Status: Fixed.**

**Resolution:** In `emit_pipeline`'s `SearchPreamble` branch, the `scan_all_rows=True`
case assigned to `offset` inside a for-loop body but never declared it. Any downstream
pipeline stage using `offset` (e.g. `SkipBits`) referenced an undeclared identifier.
Added `snippet.append_line("unsigned offset = 0;")` before the for-loop. The non-scan
branch was already correct (it used `unsigned offset = bitbuffer_search(...)`).
Regression test: `TestScanAllRowsDeclaresOffset`.

**Why this wasn't caught earlier:** Bug 20 blocked upstream compilation; fixing it
surfaced `ambient_weather.c`, the only shipped protocol that uses `scan_all_rows=True`.

---

## Bug 20 — Callable arguments not expanded when referencing other callables

**Status: Fixed.**

**Resolution:** Added `decoder_name`, `variant_name`, and `callables` keyword-only
parameters to `_expand_callable_args`. When a parameter name matches a callable in
scope, the function now recursively builds a nested C call (`_callable_c_name(...) +
"(" + inner_args + ")"`) instead of emitting the bare name. `_value_to_c`,
`emit_validate_call`, and `_emit_pending_validates` now thread the callables dict
through. Regression test: `TestCallableChainExpansion` exercises a three-deep
callable chain (`validate_cmd(cmd(notb(b)))`).

**Spec rule violated:** SEMANTICS.md §5.2: "At each call site the compiler passes the
corresponding field locals or the results of other `static inline` function calls."

**Why this wasn't caught earlier:** Bug 17 (`constexpr` vs C99) blocked the entire
generated codebase from compiling, masking this secondary bug. Fixing Bug 17 surfaced
it in `akhan_100F14.c`.

---

## Bug 18 — FirstValid field references not indexed inside row loop

**Status: Fixed.**

**Resolution:** Added `row_index` and `indexed_fields` optional parameters to
`_expand_callable_args`, `_value_to_c`, `emit_data_make`, `emit_validate_call`,
`_emit_pending_validates`, and `_emit_decode_tail`. In `emit_decode_fn`, the FirstValid
loop now builds a set of sub-field names and passes `row_index="_r"` and that set through
the call chain. Any field name in the set gets `[_r]` appended at use sites. Regression
tests: `TestFirstValidWithVariants`.

---

## Bug 17 — `static constexpr` in function definitions is not valid C

**Status: Fixed.**

**Resolution:** Changed `static constexpr` to `static inline` in `codegen.py` line 500
and all comments. Removed `-std=c23` per-file override from `src/CMakeLists.txt`. Updated
SEMANTICS.md to use `static inline` throughout. C23's `constexpr` only applies to variable
declarations, not function definitions.

---

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
