#!/bin/sh
# Compile-fail tests: each tests/fail/*.vml must fail, and its output must contain the text on
# its first line ("## expect: <text>"). Packages for these tests live in tests/pkgs.
cd "$(dirname "$0")/.."
VM=./vml
fail=0
for f in tests/fail/*.vml; do
    want=$(head -1 "$f" | sed 's/^## expect: //')
    out=$($VM "$f" --lib-path tests/pkgs 2>&1)
    code=$?
    if [ $code -eq 0 ]; then
        echo "FAIL $f: compiled and ran (expected: $want)"; fail=1
    elif ! printf '%s' "$out" | grep -qF -- "$want"; then
        echo "FAIL $f: wrong error"; echo "  want: $want"; echo "  got:  $(printf '%s' "$out" | head -1)"; fail=1
    fi
done
[ $fail -eq 0 ] && echo "Compile-fail tests: PASS" || { echo "Compile-fail tests: FAILED"; exit 1; }
