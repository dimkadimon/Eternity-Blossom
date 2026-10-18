#!/bin/bash
# Hunt for a board above 441.  Each run is 300 s on 2 threads; every board is
# kept so the best can be recovered afterwards.
cd /home/user/Eternity-Blossom
BEST=441
BESTFILE=results/best_441.txt
mkdir -p scratch/hunt
for i in $(seq 1 12); do
    SEED=$((100 + i))
    OUT=scratch/hunt/run$SEED.txt
    ./build/eternity-blossom solve \
        --time 300 --threads 2 --seed $SEED \
        --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 \
        --nmove-repair --out $OUT > scratch/hunt/log$SEED.txt 2>&1
    S=$(grep -oE "matched edges *: [0-9]+" scratch/hunt/log$SEED.txt | grep -oE "[0-9]+$")
    echo "run $i seed $SEED -> $S"
    if [ -n "$S" ] && [ "$S" -gt "$BEST" ]; then
        BEST=$S
        BESTFILE=$OUT
        cp $OUT scratch/hunt/NEW_BEST_$S.txt
        echo "   *** NEW BEST $S (saved) ***"
    fi
done
echo "==== FINAL BEST $BEST in $BESTFILE"
