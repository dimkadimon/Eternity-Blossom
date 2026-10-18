#!/bin/bash
# Take a board produced by Blackwood's constructive solver and push it with the
# Eternity-Blossom local-optimisation stack: exact assignment -> cycle cover ->
# ruin & recreate -> T-join, repeated to a fixed point, then SA.
cd /home/user/Eternity-Blossom
B=${1:?board}
OUT=${2:?out}
T=$(mktemp /home/user/Eternity-Blossom/scratch/pw.XXXXXX)
cp "$B" "$T"
best=$(./build/eternity-blossom verify --board "$T" 2>&1 | grep -oE "matched [0-9]+" | head -1 | grep -oE "[0-9]+")
echo "start: $best"

for round in $(seq 1 40); do
    improved=0
    for op in "cyccover --time 15 --rounds 5000 --set unhappy-hood --cells 48" \
              "cyccover --time 15 --rounds 5000" \
              "blast --time 15 --rounds 5000" \
              "tjoin --time 15 --rounds 5000"; do
        ./build/eternity-blossom $op --board "$T" --out "$T.next" >/dev/null 2>&1
        if [ -s "$T.next" ]; then
            s=$(./build/eternity-blossom verify --board "$T.next" 2>&1 | grep -oE "matched [0-9]+" | head -1 | grep -oE "[0-9]+")
            if [ -n "$s" ] && [ "$s" -gt "$best" ]; then
                best=$s
                mv "$T.next" "$T"
                echo "  round $round: ${op%% *} -> $best"
                improved=1
            else
                rm -f "$T.next"
            fi
        fi
    done
    [ "$improved" -eq 0 ] && break
done
cp "$T" "$OUT"
echo "final after exact operators: $best"
