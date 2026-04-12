---
name: Codegen refactor patterns
description: Common duplication and inconsistency patterns found in proto_compiler codegen/dsl refactors
type: feedback
---

When a helper function is extracted to eliminate copy-paste, check that ALL call sites use the helper — not just most. The LiteralSpec emission block has appeared in both `emit_fields` and `_emit_sub_fields` even after the sub-field extraction refactor.

When a function is parameterized with a dict (e.g. `callables`), verify that the function body consistently uses the parameter rather than reaching back through a captured `cls` reference. The `_emit_decode_tail` function accepted `callables` but called `emit_data_make(records, decoder_name, None, cls._callables)` — using `cls._callables` instead of `callables`.

**Why:** These are subtle violations of "Extract rather than copy-paste" and "One implementation per concept" that survive because tests pass (the values are equivalent at runtime) but leave the code vulnerable to future divergence.

**How to apply:** After any extraction refactor, grep for the exact body that was extracted and verify zero remaining copies. Also verify that all parameters of extracted functions are actually used in their bodies.
