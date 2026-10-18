# Is 466 a plateau or a time limit?

It is a plateau.  Four things we tried this session to break it, all negative,
and one of them settles the question directly.

## The direct test: six times the annealing changes nothing

The pipeline runs 150 s of simulated annealing per board.  Run one board for
900 s instead:

| annealing time | score |
|---|---|
| 150 s | **463** |
| 900 s | **462** |

Six times the compute, one edge *worse*.  Whatever is holding these boards at
462–466 is the structure of the local optima, not the clock.  This is the single
most useful negative in the session, because it rules out the obvious answer:
you cannot buy a 467 by waiting.

What is left is the spread between independent short runs, so the game is
samples per hour, not seconds per run.

## Region LNS: no gain

`solve --region --region-size k` (destroy a patch of k cells, re-optimise it
exactly with branch and bound) was wired into `CmdSolve` but had never been
executed.  From the same board where plain annealing scores 464:

| | score |
|---|---|
| plain SA | 464 |
| `--region --region-size 6` | 463 |
| `--region --region-size 8` | 464 |
| `--region --region-size 12` | 464 |

Exactly-optimal repair of an 8- or 12-cell patch does not move a board that is
already this good.  See also `HOLEFILL.md`: exact repair only ever paid on
boards that were much further from converged.

## The colour-count bound is vacuous, but says something sharp

Count piece-edge colours over all 256 pieces and pair them off, remembering that
the 64 border slots must be grey:

```
total piece-edges 1024, 23 colours
grey edges: 64  ->  the border needs exactly 64
UPPER BOUND on matched internal edges: 480 / 480
```

Every non-grey colour occurs an even number of times and every grey edge is
accounted for by the border, so simple counting cannot rule out a perfect
solution — the bound is useless for gauging headroom.  (As it must be: Eternity
II was constructed to have a solution.)

But it does give one hard structural rule worth knowing: **there are exactly 64
grey piece-edges for exactly 64 border slots, so a grey edge may never appear
internally.**  Checked on the 466 board — its 14 mismatches involve colours
2, 15, 12 (four each) and eight others, and **none** involves grey.  There is no
free edge hiding there.

## Best-of-N: the distribution, measured

`~/EternityII_Solver/grind13.sh` samples Blackwood's DFS at break budget 13
(120 s, convert, exact fill) and keeps the best.  Nine runs:

```
459 463 462 461 464 465 463 463 463     max 465
```

Pooled with the seven budget-13 boards from `BREAKBUDGET.md` (463 463 463 462
464 466 466), that is **16 samples, mean 463.1, max 466, and 0 at 467 or
better.**  At the observed spread (sd ≈ 1.5) a 467 is roughly a 2.3σ draw, about
1 in 85 samples — a few hours of grinding at 30 samples/hour, and the tail
estimate is fragile because 16 samples is not enough to know it.

## What every route actually produces

| route | per sample | mean | max |
|---|---|---|---|
| 40 s DFS(b10) + fill + polish + 150 s SA | 190 s | 463.5 | 466 |
| DFS(b13) + exact fill, no annealing | 120 s | 463.1 | 466 |
| DFS(b13) + fill + polish + SA | 270 s | ~464 | 466 |
| annealing for 900 s instead of 150 s | 900 s | — | 462 |
| crossover over 16 of our own boards | — | — | 465 |
| 64 disjoint holes + optimal fill (see `DISJOINT_HOLES.md`) | — | — | ~350 |

They all stop at 466.  Two structurally different 466 boards exist
(`results/best_466.txt`, 8-hole lineage; `results/best_466_b13.txt`, 2-hole
lineage), so the ceiling is not an artefact of one family of boards.

## Where that leaves it

466 is one short of the lowest published result (467) and four short of the
record (470), and the published 467–470 results all used memetic/population
search with crossover.  Our crossover attempt converged in four generations on a
population of 16 near-identical boards; the budget-13 generator now gives
structurally different boards (2 holes instead of 8, breaks in different
places), which is the diversity that attempt lacked.  That, or simply grinding
samples, is what is left.
