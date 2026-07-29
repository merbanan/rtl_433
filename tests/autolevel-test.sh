#!/bin/sh
#
# Black-box test for the pulse detector auto level, which is on by default.
#
# Two things are checked. First, that every documented spelling of
# "-Y autolevel" and its conf file equivalent turns the feature on or off as
# advertised. Second, that the auto-tracked detection floor actually moves,
# stays inside its clamp, and settles instead of re-adjusting forever.
#
# The behavioural half runs rtl_433 over generated cu8 files. A "quiet" file of
# low-amplitude samples lets the tracked floor walk down; a "silent" file of
# dead-centre samples drives it into the -30 dB clamp. Both are noise-only, so
# no decoder ever fires and the test is not tied to any device.
#
# Usage: autolevel-test.sh [path-to-rtl_433-binary]
#   Defaults to ../src/rtl_433 relative to this script.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RTL_433=${1:-"$SCRIPT_DIR/../src/rtl_433"}

if [ ! -x "$RTL_433" ]; then
    echo "ERROR: rtl_433 binary not found or not executable: $RTL_433" >&2
    exit 99
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found, cannot generate sample files" >&2
    exit 77
fi

WORK_DIR=$(mktemp -d 2>/dev/null || echo /tmp/rtl_433_autolevel_test.$$)
mkdir -p "$WORK_DIR" || exit 98
FAILED=0
PASSED=0

cleanup() { rm -rf "$WORK_DIR"; }
trap cleanup EXIT INT TERM

fail() { echo "  NOT OK: $1" >&2; FAILED=$((FAILED + 1)); }
pass() { echo "  ok: $1"; PASSED=$((PASSED + 1)); }

EMPTY_CU8="$WORK_DIR/empty.cu8"
QUIET_CU8="$WORK_DIR/quiet.cu8"
SILENT_CU8="$WORK_DIR/silent.cu8"
CONF_ON="$WORK_DIR/autolevel-on.conf"
CONF_OFF="$WORK_DIR/autolevel-off.conf"

: > "$EMPTY_CU8"

# cu8 is complex unsigned 8-bit centred on 128. A tight spread around the
# centre is a quiet band well below the -12 dB default minimum level; dead
# centre is quieter still and drives the tracked floor into its clamp.
python3 - "$QUIET_CU8" "$SILENT_CU8" <<'EOF' || exit 98
import random, sys
random.seed(1)
quiet = bytearray()
for _ in range(2_000_000):
    quiet.append(128 + random.randint(-2, 2))
    quiet.append(128 + random.randint(-2, 2))
open(sys.argv[1], 'wb').write(bytes(quiet))
open(sys.argv[2], 'wb').write(bytes(bytearray([128]) * 8_000_000))
EOF

printf 'pulse_detect autolevel\n'   > "$CONF_ON"
printf 'pulse_detect autolevel=0\n' > "$CONF_OFF"

NOTICE='Auto Level is now enabled by default'
ADJUST='adjusting minimum detection level'

# Mind the streams: the one-time notice is written straight to stderr, matching
# the older "New defaults active" notice, while every print_logf() message goes
# to stdout. So the two counters below deliberately read different streams.

# count_notice ARGS... -- how many times the new-default notice was printed
count_notice() {
    "$RTL_433" "$@" -r "$EMPTY_CU8" 2>&1 >/dev/null | grep -c "$NOTICE"
}

# count_adjust FILE ARGS... -- how many times the floor was adjusted.
# Needs -vv: the adjustment is logged at a level the default verbosity hides.
count_adjust() {
    file=$1; shift
    "$RTL_433" -vv "$@" -r "$file" 2>/dev/null | grep -c "$ADJUST"
}

# adjust_log ARGS... -- the floor adjustment lines for the silent sample
adjust_log() {
    "$RTL_433" -vv -r "$SILENT_CU8" 2>/dev/null | grep "$ADJUST"
}

# expect_notice DESC WANT ARGS...
expect_notice() {
    desc=$1; want=$2; shift 2
    got=$(count_notice "$@")
    [ "$got" = "$want" ] && pass "$desc" || fail "$desc: expected $want notice(s), got $got"
}

# expect_enabled DESC ARGS... -- auto level on means the floor tracks down
expect_enabled() {
    desc=$1; shift
    got=$(count_adjust "$QUIET_CU8" "$@")
    [ "$got" -gt 0 ] && pass "$desc" || fail "$desc: expected the floor to track, got $got adjustments"
}

# expect_disabled DESC ARGS... -- auto level off means the floor never moves
expect_disabled() {
    desc=$1; shift
    got=$(count_adjust "$QUIET_CU8" "$@")
    [ "$got" = 0 ] && pass "$desc" || fail "$desc: expected a fixed floor, got $got adjustments"
}

echo "Testing auto level with $RTL_433"

# The one-time notice tells apart "on because of the new default" from "on
# because the user asked", so it is the observable for auto_level_set. Users of
# the shipped example conf have asked explicitly and must not be nagged.
echo "autolevel::notice:"
expect_notice "no option prints the notice"            1
expect_notice "-Y autolevel is explicit, no notice"    0 -Y autolevel
expect_notice "-Y autolevel=0 is explicit, no notice"  0 -Y autolevel=0
expect_notice "-Y autolevel=1 is explicit, no notice"  0 -Y autolevel=1
expect_notice "conf file autolevel, no notice"         0 -c "$CONF_ON"
expect_notice "conf file autolevel=0, no notice"       0 -c "$CONF_OFF"

# Every spelling the usage text and docs promise. The off words are matched
# case-insensitively, so check a capitalised one too.
echo "autolevel::spellings:"
expect_enabled  "on by default"
expect_enabled  "-Y autolevel"          -Y autolevel
expect_enabled  "-Y autolevel=1"        -Y autolevel=1
expect_disabled "-Y autolevel=0"        -Y autolevel=0
expect_disabled "-Y autolevel=no"       -Y autolevel=no
expect_disabled "-Y autolevel=off"      -Y autolevel=off
expect_disabled "-Y autolevel=OFF"      -Y autolevel=OFF
expect_disabled "-Y autolevel=No"       -Y autolevel=No
expect_enabled  "conf file autolevel"   -c "$CONF_ON"
expect_disabled "conf file autolevel=0" -c "$CONF_OFF"

# The floor must stop at the built-in clamp no matter how quiet the band is,
# and having stopped there it must not keep re-adjusting. Re-adjusting on every
# frame is a real regression this catches: it re-arms the pulse detector and
# logs a warning per frame for as long as rtl_433 runs.
echo "autolevel::clamp:"
clamped=$(adjust_log | tail -1)
case "$clamped" in
    *"detection level to -30.0 dB"*) pass "a silent band settles at the -30.0 dB clamp" ;;
    *) fail "expected the floor to settle at -30.0 dB, last adjustment was: ${clamped:-none}" ;;
esac

at_clamp=$(adjust_log | grep -c "detection level to -30.0 dB")
[ "$at_clamp" = 1 ] \
    && pass "the clamped floor is applied once, not re-adjusted per frame" \
    || fail "expected 1 adjustment to the clamp, got $at_clamp"

# Noise estimation keeps falling past the clamp, so a floor that ignored the
# clamp would have gone below -30 dB here.
below=$(adjust_log \
    | sed -n "s/.*detection level to \(-[0-9]*\)\..* dB.*/\1/p" \
    | awk '$1 < -30 { n++ } END { print n + 0 }')
[ "$below" = 0 ] \
    && pass "the floor never goes below the clamp" \
    || fail "expected no adjustment below -30 dB, got $below"

echo
echo "auto level: $PASSED passed, $FAILED failed."
[ "$FAILED" -eq 0 ] || exit 1
exit 0
