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
#
# Every run passes -c /dev/null unless the case is about a conf file. Without it
# rtl_433 searches ./rtl_433.conf, $XDG_CONFIG_HOME/rtl_433/rtl_433.conf and
# $SYSCONFDIR/rtl_433/rtl_433.conf, and the shipped example conf has an active
# "pulse_detect autolevel" line, so an installed conf would decide the result
# instead of the option under test.
NO_CONF="-c /dev/null"

# RC carries the exit status of the last count_* run, so callers can tell "the
# floor did not move" apart from "rtl_433 refused to start". Without that an
# option that fatally exits looks exactly like an option that works.
RC=0

# count_notice ARGS... -- how many times the new-default notice was printed
count_notice() {
    out=$("$RTL_433" "$@" -r "$EMPTY_CU8" 2>&1 >/dev/null)
    RC=$?
    printf '%s\n' "$out" | grep -c "$NOTICE"
}

# count_adjust FILE ARGS... -- how many times the floor was adjusted.
# Needs -vv: the adjustment is logged at a level the default verbosity hides.
count_adjust() {
    file=$1; shift
    out=$("$RTL_433" -vv "$@" -r "$file" 2>/dev/null)
    RC=$?
    printf '%s\n' "$out" | grep -c "$ADJUST"
}

# adjust_log -- the floor adjustment lines for the silent sample
adjust_log() {
    "$RTL_433" -vv $NO_CONF -r "$SILENT_CU8" 2>/dev/null | grep "$ADJUST"
}

# expect_notice DESC WANT ARGS...
expect_notice() {
    desc=$1; want=$2; shift 2
    got=$(count_notice "$@")
    if [ "$RC" != 0 ]; then
        fail "$desc: rtl_433 exited $RC"
    elif [ "$got" = "$want" ]; then
        pass "$desc"
    else
        fail "$desc: expected $want notice(s), got $got"
    fi
}

# expect_enabled DESC ARGS... -- auto level on means the floor tracks down
expect_enabled() {
    desc=$1; shift
    got=$(count_adjust "$QUIET_CU8" "$@")
    if [ "$RC" != 0 ]; then
        fail "$desc: rtl_433 exited $RC"
    elif [ "$got" -gt 0 ]; then
        pass "$desc"
    else
        fail "$desc: expected the floor to track, got $got adjustments"
    fi
}

# expect_disabled DESC ARGS... -- auto level off means the floor never moves.
# The exit status check is the point here: a fatal exit also produces no
# adjustments, and without it every off-spelling case would pass on a binary
# that rejects the option outright.
expect_disabled() {
    desc=$1; shift
    got=$(count_adjust "$QUIET_CU8" "$@")
    if [ "$RC" != 0 ]; then
        fail "$desc: rtl_433 exited $RC"
    elif [ "$got" = 0 ]; then
        pass "$desc"
    else
        fail "$desc: expected a fixed floor, got $got adjustments"
    fi
}

echo "Testing auto level with $RTL_433"

# The one-time notice tells apart "on because of the new default" from "on
# because the user asked", so it is the observable for auto_level_set. Users of
# the shipped example conf have asked explicitly and must not be nagged.
echo "autolevel::notice:"
expect_notice "no option prints the notice"            1 $NO_CONF
expect_notice "-Y autolevel is explicit, no notice"    0 $NO_CONF -Y autolevel
expect_notice "-Y autolevel=0 is explicit, no notice"  0 $NO_CONF -Y autolevel=0
expect_notice "-Y autolevel=1 is explicit, no notice"  0 $NO_CONF -Y autolevel=1
expect_notice "conf file autolevel, no notice"         0 -c "$CONF_ON"
expect_notice "conf file autolevel=0, no notice"       0 -c "$CONF_OFF"

# Every spelling atobv() accepts, in both directions. The words are matched
# case-insensitively, so check a capitalised one too. The false/disable/n cases
# are the ones that matter most: they are what a user reaching for an opt-out
# is likely to type, and parsing that silently treats an unrecognised word as
# "on" would turn the feature ON for someone trying to turn it off.
echo "autolevel::spellings:"
expect_enabled  "on by default"           $NO_CONF
expect_enabled  "-Y autolevel"            $NO_CONF -Y autolevel
expect_enabled  "-Y autolevel=1"          $NO_CONF -Y autolevel=1
expect_enabled  "-Y autolevel=on"         $NO_CONF -Y autolevel=on
expect_enabled  "-Y autolevel=yes"        $NO_CONF -Y autolevel=yes
expect_enabled  "-Y autolevel=true"       $NO_CONF -Y autolevel=true
expect_enabled  "-Y autolevel=enable"     $NO_CONF -Y autolevel=enable
expect_disabled "-Y autolevel=0"          $NO_CONF -Y autolevel=0
expect_disabled "-Y autolevel=no"         $NO_CONF -Y autolevel=no
expect_disabled "-Y autolevel=off"        $NO_CONF -Y autolevel=off
expect_disabled "-Y autolevel=OFF"        $NO_CONF -Y autolevel=OFF
expect_disabled "-Y autolevel=No"         $NO_CONF -Y autolevel=No
expect_disabled "-Y autolevel=false"      $NO_CONF -Y autolevel=false
expect_disabled "-Y autolevel=disable"    $NO_CONF -Y autolevel=disable
expect_enabled  "conf file autolevel"     -c "$CONF_ON"
expect_disabled "conf file autolevel=0"   -c "$CONF_OFF"

# The floor must stop at the built-in clamp no matter how quiet the band is,
# and having stopped there it must not keep re-adjusting. Re-adjusting on every
# frame is a real regression this catches: it re-arms the pulse detector and
# logs a warning per frame for as long as rtl_433 runs.
# An explicit -Y minlevel is a request for that detection level, so it turns the
# new default off rather than letting the tracked floor walk away from it. The
# trigger is "noise 3 dB below minlevel", so without this a user raising
# minlevel to cut load would make autolevel engage sooner and settle lower than
# the -12 dB default they were trying to get away from. An explicit -Y autolevel
# still wins in either direction.
echo "autolevel::minlevel:"
expect_disabled "-Y minlevel alone turns the default off"      $NO_CONF -Y minlevel=-10
expect_enabled  "-Y minlevel with explicit autolevel tracks"   $NO_CONF -Y minlevel=-10 -Y autolevel
expect_enabled  "order does not matter, autolevel first"       $NO_CONF -Y autolevel -Y minlevel=-10
expect_disabled "-Y minlevel with explicit autolevel=0"        $NO_CONF -Y minlevel=-10 -Y autolevel=0
expect_enabled  "no minlevel still gets the default"           $NO_CONF
expect_notice   "-Y minlevel suppresses the notice too"      0 $NO_CONF -Y minlevel=-10

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
# clamp would have gone below -30 dB here. Compare the whole number, fraction
# included: truncating to the integer part would let anything from -30.1 to
# -30.9 dB through and only catch a breakage of a full dB or more.
below=$(adjust_log \
    | sed -n "s/.*detection level to \(-\{0,1\}[0-9]\{1,\}\.[0-9]\{1,\}\) dB.*/\1/p" \
    | awk '$1 < -30 { n++ } END { print n + 0 }')
[ "$below" = 0 ] \
    && pass "the floor never goes below the clamp" \
    || fail "expected no adjustment below -30 dB, got $below"

# A minimum level configured below the clamp must win over it. Autolevel is on
# by default now, so without this a user who asked for -Y minlevel=-35 would
# have the floor quietly raised to the -30 dB clamp and lose 5 dB of the
# sensitivity they explicitly configured. Nothing may move the floor above the
# configured level; tracking it no further down is fine.
# -Y autolevel is explicit here on purpose: minlevel alone now turns tracking
# off, which would make this case pass without ever exercising the clamp.
raised=$("$RTL_433" -vv $NO_CONF -Y minlevel=-35 -Y autolevel -r "$SILENT_CU8" 2>/dev/null \
    | sed -n "s/.*detection level to \(-\{0,1\}[0-9]\{1,\}\.[0-9]\{1,\}\) dB.*/\1/p" \
    | awk '$1 > -35.0 { n++ } END { print n + 0 }')
[ "$raised" = 0 ] \
    && pass "-Y minlevel below the clamp is not raised back up to it" \
    || fail "expected -Y minlevel=-35 to be honored, but the floor was raised $raised time(s)"

echo
echo "auto level: $PASSED passed, $FAILED failed."
[ "$FAILED" -eq 0 ] || exit 1
exit 0
