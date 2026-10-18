# Crossover, take two: a diverse population, and crossover measured against itself

`CROSSOVER.md` recorded a memetic run that converged in four generations and
diagnosed the cause as a population of 16 near-identical boards.  This is the
retry with a population built from two genuinely different lineages — and with
crossover switched off as a control, which the first attempt never had.

## What the operator actually does

`CrossoverRegion` (`src/popsearch.cpp:15`) copies parent B, transplants parent
A's rectangle **at the same board position**, and then repairs the damage:

* the pieces A puts into the rectangle are now duplicated — they also sit
  somewhere else in B — so those other cells are *freed*;
* the pieces B had in the rectangle but A does not are *displaced*;
* the two sets are always the same size, and `repairAssign` re-homes the
  displaced pieces into the freed cells by optimal bipartite assignment.

So a crossover is a transplant plus an optimal re-homing.  Two consequences
matter.  First, the freed cells are wherever the duplicates happened to be —
anywhere on the board, not near the rectangle — so even a 1×1 transplant moves a
piece to an unrelated neighbourhood.  Second, the operator is a *perturbation*,
not a recombination of two good partial solutions: there is no mechanism by
which a child inherits the good parts of both parents, so there is no reason to
expect it to beat both, and it never does.

## The population

16 seeds, 8 per lineage, named so that any population size takes a balanced mix
(the loader uses the first `min(popSize, n)` files in sorted order):

```
s01_a  466  best_466_b13      s02_b  465  st1_exp_407
s03_a  466  best_466          s04_b  465  st1_cheap_402
s05_a  465  gf_…4457289530    s06_b  463  st1_exp_408
s07_a  464  r13f_5            s08_b  463  st1_exp_406
s09_a  464  gf_…3223289530    s10_b  463  st1_exp_401
s11_a  464  conv_b16          s12_b  462  st1_exp_405
s13_a  463  r13f_3            s14_b  462  st1_cheap_408
s15_a  463  gf_…804458722927  s16_b  461  st1_exp_404
```

Lineage **a** is the break-budget-13 line (2-hole DFS, see `BREAKBUDGET.md`);
lineage **b** is the old 8-hole pipeline.  Supplied boards enter the population
unchanged (`popsearch.cpp:131` — no initial local search is run on them).

## Region size is the knob that matters

`--reg-min/--reg-max` are the **side lengths** of the transplanted rectangle, so
the default 3..9 transplants 9 to **81 cells** — up to a third of the board.
Child quality, measured before any refinement (parent cost is 28, so lower is
better and anything below 28 would be a child better than the best parent):

| region | cells transplanted | child cost, mean | child cost, best | improvements |
|---|---|---|---|---|
| 3..9 (default) | 9–81 | 147 | 28 | 1 |
| 2..6 | 4–36 | 84 | 28 | 4 |
| 2..4 | 4–16 | 79 | 28 | 4 |
| 1..3 | 1–9 | 49 | 28 | 4 |
| 1..2 | 1–4 | 40 | 28 | 4 |

The default was transplanting a third of the board and then trying to anneal the
rubble back into shape.  Shrinking it is a large, cheap win — but notice the
last column: **the best child equals the best parent at every setting, and never
once goes below it.**  Across every configuration in this session, hundreds of
crossovers produced no child better than the best parent.

## The control: crossover on versus off

900 s, population 12, 12 s refinement per child, region 1..3, `--breed-t0 1.0`:

| | crossover **on** | crossover **off** |
|---|---|---|
| population mean, gen 1 | 31 | 31 |
| population mean, gen 8 | **28** | 31 |
| best | 28 (466/480) | 28 (466/480) |
| crossovers / improvements | 75 / 12 | 0 / 0 |

This is the clean statement of what crossover buys.  It is not nothing: it drags
the weak individuals up to elite quality, pulling the whole population to 466
within two generations.  But it is **pure exploitation** — it spreads the best
material around and never improves on it.  With crossover off the population
just sits where it started.  Either way the best is 466.

Hot versus cold refinement (`--breed-t0` 0.30 / 1.0 / 1.5 / 5.0) changes the
number of improvements but not the outcome: the ceiling is 466 at every
temperature.

## Reproducing

```bash
# build the seed set (scores and lineage-aware names are computed for you)
./build/eternity-blossom pop --boards scratch/part/xseeds --pop 12 --breed 12 \
    --breed-t0 1.0 --reg-min 1 --reg-max 3 --time 900 --seed 21 --trace \
    --out scratch/part/popX.txt
# the control
    … --no-crossover …
```

The diagnostic to watch is `child cost BEFORE refinement: mean … best …
(best parent …)`.  It separates "crossover is producing good raw material and
the refinement is too weak to polish it" (best child < best parent) from
"crossover is a random kick" (best child = best parent).  It is the second case,
at every setting.

## Verdict

Crossover does not beat 466 here, and the reason is structural rather than a
matter of tuning.  A transplant that must re-home displaced pieces by optimal
assignment cannot combine two parents' strengths; it can only perturb one.  The
published 467–470 results used memetic search, but with recombination that keeps
the piece set consistent rather than repairing duplication after the fact —
that is the piece this operator is missing.
