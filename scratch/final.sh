#!/bin/bash
# Final attempt: the best configuration measured, 300 s x 2 threads, repeated.
# mean 438.0 sd 1.54 over 12 runs, so P(one run >= 442) is only about 0.5%;
# this is a long shot and is recorded as such.
cd /home/user/Eternity-Blossom
mkdir -p scratch/final
BEST=441
for i in $(seq 1 12); do
    SEED=$((300 + i))
    ./build/eternity-blossom solve --time 300 --threads 2 --seed $SEED \
        --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 \
        --nmove-repair --out scratch/final/f$SEED.txt > scratch/final/l$SEED.txt 2>&1
    S=$(grep -oE "matched edges *: [0-9]+" scratch/final/l$SEED.txt | grep -oE "[0-9]+$")
    echo "run $i seed $SEED -> $S"
    if [ -n "$S" ] && [ "$S" -gt "$BEST" ]; then
        BEST=$S
        cp scratch/final/f$SEED.txt scratch/hunt_best_$S.txt
        echo "    *** NEW BEST $S ***"
    fi
done
echo "==== FINAL BEST $BEST"
