---
name: E2E Test Pass (2026-04-12)
description: First e2e run after Bugs 14-22 resolved — all tests pass
type: project
---

## Full Success
**Date:** 2026-04-12

All three test stages passed:
- **Unit tests:** 93/93 pass (`proto_compiler/test/`)
- **Build:** Clean, no errors
- **E2E tests:** 1827/1827 pass (zero failures)

## Context
This was the first full e2e test run after resolving COMPILER_BUGS entries 14–22, which spanned core features:
- Callable chain expansion (Bug 20)
- FirstValid row indexing (Bug 18)
- Variant JSON synthesis (Bug 15)
- Dispatcher variant methods (Bug 22)
- And others (acurite_01185m, ambient_weather, akhan_100F14 specific fixes)

## Proto-Compiled Decoder Status
All 11 proto-compiled protocols in `proto_compiler/protocols/` now generate valid C and pass e2e:
abmt, acurite_01185m, akhan_100F14, alectov1, ambient_weather, current_cost, honeywell_wdb, honeywell_wdb_fsk, lacrosse_tx31u, thermopro_tp211b, tpms_ford

## What Changed
The compiler pipeline went from broken (Bugs 14–22 blocking compilation or producing wrong code) to fully functional (all test categories green).

## Next Steps
No bugs to file. Future conversations should focus on new development or test-exposed issues. If a new test failure appears, check COMPILER_BUGS.md first to rule out regressions.
