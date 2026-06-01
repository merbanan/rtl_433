#!/bin/sh
#
# Configure, build, and run the test suite with code-coverage instrumentation,
# then produce a coverage report. Focuses the report on the HTTP server but
# gcovr/lcov see the whole tree.
#
# Requires gcovr (apt install gcovr, or pip install gcovr) for the HTML/text
# report; falls back to a raw `gcov` summary if gcovr is not installed.
#
# Usage: tests/coverage.sh [build-dir]
#   build-dir defaults to ./build-coverage

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${1:-"$SRC_DIR/build-coverage"}

echo ">> Configuring coverage build in $BUILD_DIR"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DENABLE_COVERAGE=ON \
    -DBUILD_TESTING=ON \
    -DENABLE_RTLSDR=OFF \
    -DENABLE_SOAPYSDR=OFF \
    -DCMAKE_BUILD_TYPE=None

echo ">> Building"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 2)"

echo ">> Running tests (ctest)"
# Don't abort the report if a test fails; we still want coverage of what ran,
# but remember the status so we can propagate it as our exit code (CI should
# fail on a test failure even though the report is still produced/uploaded).
ctest_rc=0
( cd "$BUILD_DIR" && ctest --output-on-failure ) || ctest_rc=$?
[ "$ctest_rc" -eq 0 ] || echo ">> (some tests failed, rc=$ctest_rc; continuing to report)"

echo ">> Generating coverage report"
if command -v gcovr >/dev/null 2>&1; then
    mkdir -p "$BUILD_DIR/coverage-report"
    # Whole-project summary to the terminal...
    gcovr --root "$SRC_DIR" --object-directory "$BUILD_DIR" \
        --exclude '.*/mongoose\.c' --exclude '.*/jsmn\.c' --print-summary \
        --html-details "$BUILD_DIR/coverage-report/index.html"
    echo
    echo ">> HTTP server coverage:"
    gcovr --root "$SRC_DIR" --object-directory "$BUILD_DIR" \
        --filter '.*/http_server\.c' --txt
    echo ">> Full HTML report: $BUILD_DIR/coverage-report/index.html"
else
    echo ">> gcovr not found; install it (apt: 'apt-get install gcovr', or 'pip install gcovr') for a nice report." >&2
    echo ">> Falling back to raw gcov on http_server.c:"
    GCDA=$(find "$BUILD_DIR" -name 'http_server.c.gcda' | head -1)
    if [ -n "$GCDA" ]; then
        ( cd "$(dirname "$GCDA")" && gcov -b http_server.c.gcno | sed -n '1,12p' )
    else
        echo ">> No http_server.c.gcda found - did the server exit cleanly?" >&2
    fi
fi

# Propagate the test result so CI fails on a test failure (after reporting).
exit "$ctest_rc"
