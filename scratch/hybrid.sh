#!/bin/bash
# Hybrid: Blackwood's constructive DFS builds a deep partial board; the
# Eternity-Blossom stack finishes and polishes it.
#
#   Blackwood DFS  ->  partial board (index ~247/256)
#                  ->  exact bipartite assignment fills the empty cells
#                  ->  cycle cover / ruin & recreate / T-join to a fixed point
#                  ->  SA with ILS + post-kick repair
#
# Neither half gets here alone: the DFS cannot finish the last few cells, and
# our own search from scratch plateaus at 441.
set -u
E2=/home/user/Eternity-Blossom
BW=/home/user/EternityII_Solver/EternityII_Solver
export TMPDIR=/home/user/tmpwork
export DOTNET_ROOT=/home/user/dotnet PATH=/home/user/dotnet:$PATH
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

BW_SECONDS=${BW_SECONDS:-40}
SA_SECONDS=${SA_SECONDS:-150}
ROUNDS=${ROUNDS:-8}
BEST=${BEST:-441}
OVERALL=$E2/scratch/hybrid_best.txt
mkdir -p $E2/scratch/hybrid
[ -f "$OVERALL" ] || cp $E2/results/best_441.txt "$OVERALL"

for i in $(seq 1 $ROUNDS); do
    SEED=$((400 + i))
    RAW=$E2/scratch/hybrid/raw$SEED.txt
    CONV=$E2/scratch/hybrid/conv$SEED.txt
    FULL=$E2/scratch/hybrid/full$SEED.txt
    ST1=$E2/scratch/hybrid/st1_$SEED.txt
    FIN=$E2/scratch/hybrid/fin$SEED.txt

    # 1. constructive DFS
    (cd $BW && dotnet bin/Release/net8.0/EternityII_Solver.dll \
        --threads 2 --solves 100 --seconds $BW_SECONDS --mode opt \
        --export "$RAW" >/dev/null 2>&1)

    # 2. convert (vflip: verified by border_bad 0)
    python3 /home/user/bw2e2.py "$RAW" "$CONV" vflip >/dev/null 2>&1

    # 3. exact completion
    (cd $E2 && ./build/eternity-blossom complete --board "$CONV" --out "$FULL" >/dev/null 2>&1)

    # 4. exact operators to a fixed point
    (cd $E2 && bash scratch/polish_bw.sh "$FULL" "$ST1" >/dev/null 2>&1)

    # 5. our SA, starting from that board
    (cd $E2 && ./build/eternity-blossom solve --time $SA_SECONDS --threads 2 --seed $SEED \
        --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 --nmove-repair \
        --board "$ST1" --out "$FIN" >/dev/null 2>&1)

    S=$(cd $E2 && ./build/eternity-blossom verify --board "$FIN" 2>&1 | grep -oE "matched [0-9]+" | head -1 | grep -oE "[0-9]+")
    MID=$(cd $E2 && ./build/eternity-blossom verify --board "$ST1" 2>&1 | grep -oE "matched [0-9]+" | head -1 | grep -oE "[0-9]+")
    echo "round $i seed $SEED: completed=$MID  after SA=$S"
    if [ -n "$S" ] && [ "$S" -gt "$BEST" ]; then
        BEST=$S
        cp "$FIN" "$OVERALL"
        echo "   *** NEW BEST $S ***"
    fi
done
echo "==== HYBRID BEST $BEST (in $OVERALL)"
