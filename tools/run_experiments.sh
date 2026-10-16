#!/usr/bin/env bash
# Eternity-Blossom experiment harness.
#
# Runs the solver under matched configurations and writes a CSV so the numbers in
# experiments/RESULTS.md can be regenerated.  The point of most of these runs is
# an ABLATION: switch one matching operator off and see what it costs.
#
# usage:  tools/run_experiments.sh [output.csv] [seconds_per_run] [seeds...]

set -u
BIN=${BIN:-./build/eternity-blossom}
OUT=${1:-results/experiments.csv}
TIME=${2:-25}
SEEDS=${3:-"1 2"}
mkdir -p "$(dirname "$OUT")"

echo "instance,config,seed,seconds,matched,max,iterations,blossom_passes,blossom_gain,assign_passes,assign_gain" > "$OUT"

run() {
    local inst=$1 pieces=$2 cfg=$3 seed=$4
    local extra=""
    case "$cfg" in
        full)      extra="" ;;
        noblossom) extra="--noblossom" ;;
        noassign)  extra="--assign 0" ;;
        plain)     extra="--noblossom --assign 0" ;;
    esac
    local log
    log=$($BIN solve --pieces "$pieces" --time "$TIME" --seed "$seed" \
             --sample 16 --accept sa --t0 3.0 --t1 0.1 --w "${W:-0}" $extra \
             --out /tmp/exp_board.txt --json 2>/dev/null)
    local matched max iters bp bg ap ag
    matched=$(echo "$log" | sed -n 's/^matched edges : \([0-9]*\) .*/\1/p')
    max=$(echo "$log" | sed -n 's/^matched edges : [0-9]* \/ \([0-9]*\)/\1/p')
    iters=$(echo "$log" | sed -n 's/^iterations    : \([0-9]*\)/\1/p')
    bp=$(echo "$log" | sed -n 's/^blossom passes: \([0-9]*\).*/\1/p')
    bg=$(echo "$log" | sed -n 's/^blossom gain  : predicted [0-9]*, measured \([0-9-]*\)/\1/p')
    ap=$(echo "$log" | sed -n 's/^assign passes : \([0-9]*\).*/\1/p')
    ag=$(echo "$log" | sed -n 's/^assign passes : [0-9]* (improved [0-9]*)  measured gain \([0-9-]*\)/\1/p')
    echo "$inst,$cfg,$seed,$TIME,${matched:-0},${max:-0},${iters:-0},${bp:-0},${bg:-0},${ap:-0},${ag:-0}" >> "$OUT"
    echo "  $inst $cfg seed=$seed -> ${matched:-?}/${max:-?}"
}

for inst in ${INSTANCES:-}; do
    name=${inst%%:*}; pieces=${inst#*:}
    echo "== $name ($pieces) =="
    for seed in $SEEDS; do
        for cfg in full noblossom noassign plain; do
            run "$name" "$pieces" "$cfg" "$seed"
        done
    done
done
echo "wrote $OUT"
