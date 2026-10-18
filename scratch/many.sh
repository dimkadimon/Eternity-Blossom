#!/bin/bash
# Maximising the MAXIMUM, not the mean.
#
# Two 300 s runs beat one 600 s run on average, but they do NOT beat it on the
# maximum: the mean gains about 2 edges from 120 s -> 300 s while the number of
# independent samples drops by 2.5x.  For a record hunt only the maximum counts,
# and E[max of N] grows like mean + sd*sqrt(2 ln N) -- so more, shorter, higher
# -variance runs win.  Measured: 15 x 120 s -> mean 435.9 sd 3.2 max 442;
# 12 x 300 s x 2 thr -> mean 438.0 sd 1.6 max 441.
#
# So: many independent 120 s runs, two at a time (2 cores), keep every board.
cd /home/user/Eternity-Blossom
mkdir -p scratch/many
BEST=441
cp results/best_441.txt scratch/many/incumbent_441.txt 2>/dev/null

run_one() {
    local SEED=$1
    local OUT=scratch/many/s$SEED.txt
    ./build/eternity-blossom solve --time 120 --seed $SEED \
        --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 \
        --nmove-repair --out $OUT > scratch/many/l$SEED.txt 2>&1
    grep -oE "matched edges *: [0-9]+" scratch/many/l$SEED.txt | grep -oE "[0-9]+$"
}

# two at a time, 64 runs total
for base in $(seq 200 2 262); do
    S1=$base
    S2=$((base + 1))
    run_one $S1 > scratch/many/r$S1.val &
    P1=$!
    run_one $S2 > scratch/many/r$S2.val &
    P2=$!
    wait $P1; wait $P2
    V1=$(cat scratch/many/r$S1.val)
    V2=$(cat scratch/many/r$S2.val)
    echo "seed $S1 -> $V1 ; seed $S2 -> $V2"
    for pair in "$V1 $S1" "$V2 $S2"; do
        set -- $pair
        V=$1; SD=$2
        if [ -n "$V" ] && [ "$V" -gt "$BEST" ]; then
            BEST=$V
            cp scratch/many/s$SD.txt scratch/hunt_best_$V.txt
            cp scratch/many/s$SD.txt scratch/many/NEW_BEST_$V.txt
            echo "    *** NEW BEST $V (seed $SD) ***"
        fi
    done
done
echo "==== FINAL BEST $BEST"
