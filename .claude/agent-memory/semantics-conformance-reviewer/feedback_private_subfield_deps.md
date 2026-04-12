---
name: "Private sub-field deps silently break validate hooks"
description: "When a validate_* hook references private Rows sub-fields (prefixed with _), the dep names never enter parsed_fields, so the hook silently never fires -- no error, no warning."
type: feedback
---

Private fields in Rows/Repeat sub-protocols only advance bit_pos -- no C variable or parsed_fields entry is created. If a validate_* hook lists a parameter like `data_checksum` expecting it to map to private sub-field `_checksum`, the dependency is never satisfied and the hook is silently skipped.

**Why:** Found in acurite_01185m.py during 2026-04-11 audit. The protocol's checksum validation is completely bypassed at runtime.

**How to apply:** When reviewing protocol files, always check that validate_* parameter names match *public* (non-underscore-prefixed) sub-field names. Flag any validate_* whose deps include names not present in the public field set.
