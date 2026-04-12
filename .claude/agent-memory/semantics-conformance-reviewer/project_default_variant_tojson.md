---
name: "Default variant to_json path is broken (dead code)"
description: "_default_variant_records in codegen.py is unreachable because vcls.json_records() always returns non-empty. The actual fallback (_default_json_records) doesn't include parent fields."
type: project
---

The `or _default_variant_records(cls, vcls)` at codegen.py line 1031 is dead code. `vcls.json_records()` delegates to `_default_json_records` which always returns at least a "model" record, making the tuple truthy.

**Why:** Two design layers (dsl.py default and codegen.py default) both attempt to handle the no-to_json case but the dsl.py path wins and produces incomplete output (missing parent fields, wrong model name).

**How to apply:** Currently all variants define explicit to_json so no runtime impact. If a new variant omits to_json, this bug will surface. The fix should consolidate the default path -- either make _default_json_records variant-aware or have _emit_variants detect missing to_json directly.
