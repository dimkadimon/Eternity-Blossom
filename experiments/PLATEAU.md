# Where the score plateaus, and what it would take to move it

Numbers first, then what they mean.

## The plateau is real and it is not a throughput problem

| run | score |
|---|---|
| 120 s, seed 21 | 437 |
| 300 s, seed 21 | 441 |
| **900 s, seed 21** | **438** |

More time makes it *worse*, not better — that is noise around a plateau, not a
converging curve. Best of 8 independent 120 s runs (seeds 21/33/47/58/61/74/89/95):

```
437 437 434 436 436 438 434 438      mean 436.3, sd 1.7
```

The spread is ±2. With that distribution, best-of-1000 runs reaches roughly 446.
**Restarts cannot reach 470.**

## Why: the placement moves are exhausted, and rotation is nearly maxed out

From the 900 s run (1.22 billion iterations):

```
assign passes : 6099 (improved 100)   measured gain 304     <- exhausted
strip rot DP  : 24453 strips improved, gain 54502           <- the workhorse
blossom gain  : predicted 1564900, measured 888444
coarse swap   : 24399 passes (improved 177), 179 pairs applied
perturbations : 1422
```

The rotation strip DP is doing essentially all the work. And rotation has a hard
ceiling anyway:

```
$ eternity-blossom rotbound --board results/best_441.txt
current 441   best rotation-only bound 444
```

So on our best board, **perfect rotation of the current placement would gain 3 edges**.
Everything beyond 444 has to come from a better *placement* — and the placement moves
(assign passes: 100 improvements in 900 s) have run dry.

Neither exact operator finds anything on a converged board:

| board | bipartite assignment | non-bipartite swap | kick+LNS (4/8/16) |
|---|---|---|---|
| 424/480 | 0 | 0 | — |
| 441/480 | 0 | 0 | 441 (no improvement) |

## The 1.76x "gap" that was not a gap

The 900 s log showed `blossom gain: predicted 1564900, measured 888444`, which looks like
the non-bipartite operator delivering 57% of what it promises. It was a **reporting
bug**: `predictedGain` was the weight of the whole maximum-weight matching, recorded
*before* the adjacency filter dropped the pairs whose gains do not add up because they
touch. Those pairs were never applied.

Summing only over the pairs that survive the filter:

```
blossom gain : predicted 111550, measured 111550
```

The non-bipartite swap operator is exact. Worth writing down because that ratio sent
me looking for an algorithmic defect that did not exist.

## More dissections: real, but it runs out too

| dissections | score from random | notes |
|---|---|---|
| 400 | 346 (341–349) | |
| 1000 | 348 | |
| 3000 | **349 (341–361)** | best case 361 |
| 10000 | 347 | converges after sweep 1; the library is too big to sweep |

The operator converges after ~4 sweeps regardless of library size, so a bigger library
mostly changes *which* local optimum you land in, not how deep you go.

## What it would actually take to reach 470

470 needs +29 over our 441. Given a distribution of 436 ± 2 at 120 s and 440 ± 4 at
300 s, that is not a tuning problem.

What the published 467–470 results have in common is **memetic / population search**:
keep a population of boards, run local search on each, then *crossover* — take a region
from one parent and repair it using the other — and keep the best. Plus months of CPU
across many cores. Our solver is a single-trajectory SA/LAHC with a good exact
repertoire, which is a different and much weaker regime.

A concrete plan, in order of expected value:

1. **Population + crossover.** The single biggest lever, and the one the literature
   supports. Needs a board-difference representation, a crossover operator, and a
   replacement policy.
2. **Destroy-and-repair LNS at a larger scale** — carve out an 8×8 or 12×12 region and
   rebuild it with the exact machinery, rather than only 2×3 sub-boards.
3. **Diversification over restarts** — worth ~+6, not +29.

Sandbox constraints matter here: 2 cores and no persistence across snapshots, so
"throw months of CPU at it" is not available.

## Where non-bipartite matching can still contribute

Not for raising the score directly — the operators are not the bottleneck — but these
are genuinely different uses of Blossom and worth having:

* **T-joins for defect repair.** The cells incident to an odd number of mismatched
  edges form an even-size set T. A minimum-weight T-join is the cheapest set of edges
  along which to exchange pieces to make every cell even, and it reduces to
  minimum-weight perfect matching on the complete graph over T — non-bipartite, so
  Blossom. This is a structurally different move from anything currently implemented.
* **2-factors for cyclic shifts.** Rotating pieces around a cycle is an E2 move nobody
  here uses. A set of vertex-disjoint cycles covering some cells is a 2-factor, also
  reducible to perfect matching, also non-bipartite.
* **Odd-set certificates as cutting planes.** In the sub-board branch and bound, when
  the stub bound is inconclusive, Blossom's dual yields violated blossom inequalities
  that can tighten the bound. Roadmap item `DualCertificate()`.
* **Matching to choose the perturbation.** Instead of kicking random cells, use a
  matching to pick a set of pair swaps that are guaranteed non-interacting, so the
  perturbation's cost is predictable.
