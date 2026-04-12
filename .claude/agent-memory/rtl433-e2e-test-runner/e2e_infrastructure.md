---
name: E2E Test Infrastructure
description: Location and invocation method for rtl_433 e2e tests, typical pass/fail patterns
type: reference
---

## Test Suite Location
- Test fixtures and runner: `/Users/ali/rtl_433_tests/`
- Test runner script: `/Users/ali/rtl_433_tests/bin/run_test.py`

## How to Run E2E Tests
From `/Users/ali/rtl_433_tests/`:
```bash
PATH="/Users/ali/rtl_433/build/src:$PATH" python3 bin/run_test.py -I time --first-line
```

The `-I time` flag ignores time-based test variations; `--first-line` shows only first line of each test result.

## Build Prerequisite
Before running e2e tests, always rebuild the project from `/Users/ali/rtl_433/build` (NOT the project root):
```bash
cd /Users/ali/rtl_433/build && cmake --build .
```

The project root `/Users/ali/rtl_433/CMakeLists.txt` has stale in-source CMake configuration that lacks C23 per-file overrides. The build must use the clean build directory.

## Test Output Patterns
- WARNING lines about missing test fixture files or ignored files are normal (test suite warns about unavailable test data)
- False positive warnings between decoders are expected and not failures (different decoders can partially decode similar signal formats)
- Final line: "X records tested, Y have failed" — Y=0 means all pass
- Successful run shows this pattern: 1827 records tested, 0 have failed

## Proto-Compiled Decoders Being Tested
E2E tests include several proto-compiled decoders (see protocol_mapping.md). These are tested by feeding signal fixtures through the compiled rtl_433 binary and comparing output against expected JSON.
