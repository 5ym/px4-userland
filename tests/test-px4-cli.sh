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
    [ "$("$@" --version)" = "$name $version" ]
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
printf '%s\n' 'PX4 device-free CLI tests: PASS'
