#!/bin/sh
# Benchmark runner. Each benchmark runs RUNS times; the fastest run counts (this machine is noisy).
#   bench/run.sh          compare against bench/baseline.txt, fail if any is >10% (+2ms) slower
#   bench/run.sh --save   record a new baseline
set -e
cd "$(dirname "$0")/.."
RUNS=${RUNS:-7}
VM=./vml
TMP=${TMPDIR:-/tmp}/vml_bench.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

python3 bench/gen_compile.py 1500 > "$TMP/compile_big.vml"
LINES=$(wc -l < "$TMP/compile_big.vml" | tr -d ' ')
python3 bench/gen_compile.py 6000 > "$TMP/parse_big.vml"
PLINES=$(wc -l < "$TMP/parse_big.vml" | tr -d ' ')

# best <file> <field> [flag]: field is "Execute time", "Compile time" or "Parse time"
best() {
    b=""
    i=0
    while [ $i -lt $RUNS ]; do
        v=$($VM "$1" --time $3 | grep "$2" | sed 's/.*: *//')
        if [ -z "$b" ] || awk -v v="$v" -v b="$b" 'BEGIN{exit !(v < b)}'; then b=$v; fi
        i=$((i + 1))
    done
    echo "$b"
}

RESULTS="$TMP/results.txt"
: > "$RESULTS"
for f in bench/*.vml; do
    name=$(basename "$f" .vml)
    sec=$(best "$f" "Execute time")
    echo "$name $sec" >> "$RESULTS"
done
ms=$(best "$TMP/compile_big.vml" "Compile time")
echo "compile_${LINES}_lines $(awk -v v="$ms" 'BEGIN{printf "%.4f", v / 1000}')" >> "$RESULTS"
pms=$(best "$TMP/parse_big.vml" "Parse time" --parse-only)
echo "parse_ast_${PLINES}_lines $(awk -v v="$pms" 'BEGIN{printf "%.4f", v / 1000}')" >> "$RESULTS"

if [ "$1" = "--save" ]; then
    cp "$RESULTS" bench/baseline.txt
    echo "Saved baseline:"
    cat bench/baseline.txt
    exit 0
fi

fail=0
printf "%-26s %10s %10s %8s\n" benchmark "base(s)" "now(s)" change
while read -r name now; do
    base=$(grep "^$name " bench/baseline.txt 2>/dev/null | awk '{print $2}')
    if [ -z "$base" ]; then
        printf "%-26s %10s %10s %8s\n" "$name" "-" "$now" "new"
        continue
    fi
    pct=$(awk -v n="$now" -v b="$base" 'BEGIN{printf "%+.1f%%", (n - b) / b * 100}')
    status=""
    if awk -v n="$now" -v b="$base" 'BEGIN{exit !(n > b * 1.10 + 0.002)}'; then status="  SLOWER"; fail=1; fi
    over=""
    if awk -v n="$now" 'BEGIN{exit !(n > 0.1)}'; then over="  >0.1s"; fi
    printf "%-26s %10s %10s %8s%s%s\n" "$name" "$base" "$now" "$pct" "$status" "$over"
done < "$RESULTS"
[ $fail -eq 0 ] || { echo "Benchmark regression detected"; exit 1; }
echo "No regressions."
