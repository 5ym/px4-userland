#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

build=$1
version=$2
for program in px4d px4-ts px4ctl px4-termux; do
    if [ "$program" = px4-termux ]; then
        set -- sh "$build/$program"
    else
        set -- "$build/$program"
    fi
    name=$program
    [ "$program" != px4ctl ] || name=px4-userland
    output=$("$@" --version)
    [ "$output" = "$name $version" ]
    "$@" --help >/dev/null
    for option in --help --version --unknown; do
        status=0
        "$@" --version "$option" >/dev/null 2>&1 || status=$?
        [ "$status" -eq 2 ]
        status=0
        "$@" "$option" --version >/dev/null 2>&1 || status=$?
        [ "$status" -eq 2 ]
    done
done
for option in --list -l; do
    status=0
    "$build/px4d" "$option" >/dev/null 2>&1 || status=$?
    [ "$status" -eq 2 ]
done
"$build/px4d" --models | grep -F PX-Q3U4 >/dev/null
test_root=$(mktemp -d "${TMPDIR:-/tmp}/px4-cli-test.XXXXXX")
trap 'find "$test_root" -depth -delete' EXIT
"$build/px4d" -h >"$test_root/short-help" 2>"$test_root/help.err"
"$build/px4d" --help >"$test_root/long-help"
cmp "$test_root/short-help" "$test_root/long-help"
[ ! -s "$test_root/help.err" ]
for option in -F --firmware; do
    status=0
    "$build/px4d" --device serial:00001205000960 "$option" "$test_root/missing.bin" \
        >"$test_root/out" 2>"$test_root/$option.err" || status=$?
    [ "$status" -eq 3 ]
    [ ! -s "$test_root/out" ]
    status=0
    "$build/px4d" --device serial:00001205000960 "$option" >/dev/null 2>&1 || status=$?
    [ "$status" -eq 2 ]
done
cmp "$test_root/-F.err" "$test_root/--firmware.err"
for option in -v --verbose; do
    status=0
    "$build/px4d" --device serial:00001205000960 -F "$test_root/missing.bin" "$option" \
        >"$test_root/out" 2>"$test_root/$option.err" || status=$?
    [ "$status" -eq 3 ]
    [ ! -s "$test_root/out" ]
    grep -Fx 'px4d DEBUG: loading firmware' "$test_root/$option.err" >/dev/null
    sed '/^px4d DEBUG: loading firmware$/d' "$test_root/$option.err" >"$test_root/filtered.err"
    cmp "$test_root/filtered.err" "$test_root/--firmware.err"
    if grep -F "$test_root" "$test_root/$option.err" >/dev/null; then exit 1; fi
    if grep -F 'opening USB enclosure' "$test_root/$option.err" >/dev/null; then exit 1; fi
done
cmp "$test_root/-v.err" "$test_root/--verbose.err"
printf '%s\n' 'PX4 device-free CLI tests: PASS'
