# 467/480 — the plateau broken, and what actually did it

Three distinct **467/480** boards, each verified by both independent
implementations (C++ `verify` and the separate Python `check_board.py`):

```
results/best_467.txt       467/480  mismatched 13  border bad 0  every piece once
results/best_467_s77.txt   467/480  mismatched 13  border bad 0  every piece once
results/best_467_s99.txt   467/480  mismatched 13  border bad 0  every piece once
```

That is one short of the record (470) and level with the lowest published
result (467).

## The recipe

```bash
./build/eternity-blossom pop --boards scratch/part/xseeds --pop 12 --breed 12 \
    --breed-t0 0.60 --time 900 --seed S --trace --out out.txt
```

`--boards` is the 16-board two-lineage seed set from `CROSSOVER_TAKE2.md`
(461–466, half from the break-budget-13 line, half from the old 8-hole
pipeline).  900 seconds.  Reached 467 on seeds 33, 77 and 99, with two
different crossover operators, and the whole population converges to 467
(mean cost 26) by generation 5–6.

## What actually did it — and it was not the crossover

This is the part worth recording, because the obvious reading is wrong.  The
four runs below differ in the operator and in the refinement temperature:

| operator | `--xover-tries` | `--breed-t0` | result |
|---|---|---|---|
| rect 1..4 (new) | 256 | **0.60** | **467** |
| cycle (new) | 32 | **0.60** | **467** |
| rect 3..9 (**shipped default**) | 1 | **0.60** | **467** |
| rect 1..4 (new) | 256 | 0.30 | 466 |

**The refinement temperature is the whole story.**  The shipped operator at
`t0 = 0.60` reaches 467; the improved operator at `0.30` — the value that had
been in the code since the memetic layer was written, and the value every
earlier run used — stays at 466.  The crossover work mattered for the
diagnostics, not for the score.

And the diagnostic stays at zero in every single run:

```
children better than BOTH parents, before refinement:  0 / 75
```

75 crossovers per run, four runs, 300 crossovers total, and **not one child was
born better than both its parents.**  So the memetic layer is not recombining
anything.  What it is really doing is best-of-N: each child is a *perturbation*
of a 466-class board, and each gets its own independent 12-second local search.
75 perturbed restarts at the right temperature is enough to turn up a 467.  The
crossover is a kick generator — a useful one, since it perturbs a good board
rather than starting from scratch, but a kick generator nonetheless.

That reframes `PLATEAU.md` slightly.  It is still true that annealing one board
for six times as long does nothing (900 s scored 462 where 150 s scored 463).
But 75 *short* runs from perturbed starts is a different thing from one long
run, and that works.

## The two operators added this session

They are in the tree and measured, even though neither is what produced the 467.

**`CrossoverCycle`** — the piece-consistent recombination, `--xover cycle`.  A
rectangle transplant is not piece-consistent: the pieces A puts into the
rectangle differ from the ones B had there, so pieces are displaced and have to
be re-homed by assignment, which scatters damage over cells that had nothing to
do with the transplant.  The sets that *are* consistent are exactly the unions
of cycles of σ = a ∘ b⁻¹, because on a cycle {a(c)} = {b(c)}.  So the child is
B everywhere except on one cycle, where it is A — rotations included — and every
piece is used exactly once with **no displaced pieces and no repair step at
all.**  It is the right construction, and it did not help: cycles are long
(most get rejected by `--cycle-max`), they are scattered rather than
contiguous so they preserve almost no internal edges, and every cell on a cycle
changes by definition.  Slightly better than a bare rectangle transplant
(mean child cost 44 vs 49), worse than a rectangle with best-of-T.

**`--xover-tries T`** — draw T candidate recombinations, score each child, keep
the cheapest.  This is a real and large improvement to child quality:

| T | 1 | 8 | 32 | 128 | 512 |
|---|---|---|---|---|---|
| mean child cost before refinement | 49 | 35 | 36 | 30 | **28** |
| (best parent) | 28 | 28 | 28 | 28 | 28 |

At T=512 the average child is exactly as good as the best parent, i.e. **no
single rectangle transplant from any population member improves a 466 board.**
The boards are locally optimal against that whole neighbourhood.  Worth knowing,
and it is the sharpest form of the "crossover cannot recombine" result.

## Reproducing the seed set

`scratch/part/xseeds/` holds 16 boards named `s01_a … s16_b` so that any
population size takes a balanced mix of the two lineages (the loader uses the
first `min(popSize, n)` files in sorted order).  To rebuild it, take the best
boards from `scratch/part/gf_*.txt`, `results/best_466*.txt`,
`scratch/part/conv_b16.txt`, `scratch/part/r13f_*.txt` (lineage a) and
`scratch/part/popseeds/st1_*.txt` (lineage b), sort each by score, and
interleave.

## Next

467 is now reproducible in 900 s, which makes 468 a sampling question again —
but with a much better sampler than before (75 perturbed restarts per 900 s,
from 466-class boards, versus whole-board runs at ~19/hour).  The knobs worth
sweeping are `--breed-t0` around 0.60 and the number of children per unit time
(`--pop` × `--breed` trade-off).
