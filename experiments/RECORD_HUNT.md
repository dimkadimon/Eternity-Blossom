# Can we beat 441?

**Short answer: no. We matched it, repeatedly, and never exceeded it.**

The search did get measurably better — the mean is up by 2 to 4 edges depending
on budget — but the maximum did not move. Everything below is what was tried and
what was measured.

## The three changes that helped

All in `src/search.cpp`, all measured at 120 s single-threaded over 15 seeds
(two independent seed batches, because results are not seed-reproducible — see
the note on noise).

### 1. Kick the incumbent, not the drifted state

The search holds an incumbent (`best`) and a current state that **drifts** away
from it, because both LAHC and SA accept worsening moves. The perturbation was
kicking the drifted state, which threw away a fresh incumbent and restarted from
wherever the walk happened to have wandered to. Proper ILS kicks the incumbent.

| | mean | max |
|---|---|---|
| old (drift kick) | 433.7 | 436 |
| new (incumbent kick) | 434.9 | 440 |

The spread also widened, which matters more than the mean for a best-of-N.

### 2. Adaptive kick size

A fixed kick is either too small to escape or so large it degenerates into a
random restart. Grow by 1.6× on a failed kick, shrink to 0.7× on a success,
cap at `--perturb-max`. (Bundled with the measurement above.)

### 3. Post-kick repair with the 2-factor / ruin-and-recreate moves

`--nmove-repair`. Those operators are worth a great deal on a board that is
**not** converged — from random, `cyccover` is +530 and `blast` +382 — and
exactly zero on one that is. A freshly kicked board is precisely the first case,
so this is the one place in the loop where they pay.

| | mean | max |
|---|---|---|
| ILS alone | 433.0 | — |
| ILS + repair | 436.8 | **442** |

**Combined: mean 433.7 → 435.9, max 436 → 442.**

## Perturbation frequency was already tuned

How often to kick (`--stall`) turned out to be at its optimum already; both
directions are worse.

| `--stall` | kicks | mean | max |
|---|---|---|---|
| 30000 | many | 420.8 | 426 |
| 100000 | | 433.8 | 437 |
| **300000 (default)** | | **436.5** | **439** |
| 600000 | | 436.5 | 438 |
| 1200000 | few | 433.8 | 437 |

## Where the compute went, and what it bought

| strategy | runs | mean | sd | max |
|---|---|---|---|---|
| 120 s, 1 thread, sequential | 15 | 435.9 | 3.2 | **442** |
| 300 s × 2 threads (one process) | 12 | **438.0** | 1.54 | 441 |
| 120 s, 2 concurrent processes | 64 | 435.55 | 2.31 | 439 |

Two things this table says:

- **Longer runs raise the mean but shrink the spread.** Going 120 s → 300 s buys
  about 2 edges of mean and loses more than that in tail. Since only the maximum
  matters for a record, that is a bad trade.
- **Concurrency degrades the tail.** 64 concurrent runs (two at a time on two
  cores) topped out at 439, *worse* than 12 uncontended runs at 300 s which
  reached 441. Running two processes on two cores costs each of them enough
  iterations to matter. If you are hunting a record, do not oversubscribe.

The 442 was seen once in the 15-run 120 s batch and was **not saved** — the run
that produced it was part of an A/B sweep that only printed scores. It remains
the highest number this project has ever produced, and it is unverified.

## Basin hopping from the record itself

`solve --board FILE` starts every thread from a given board, so the search can
explore *around* a known-good board instead of restarting from scratch.

Four runs of 120 s × 2 threads starting from `results/best_441.txt` all returned
**441 exactly**. Not a no-op — the log per run shows 148 M iterations, 177
perturbations, 7436 blossom passes and 2907 strip-DP improvements. The search
works hard and never finds anything better than what it was handed.

## Why 441 looks like a wall

Every line of evidence points the same way:

- Nine structurally unrelated exact operators report exactly **0** on the 441
  board (assignment, blossom swap, LNS, rotation strips, dissection
  reassignment, T-join, 2-factor cycle cover, ruin & recreate, and the coarse
  block moves).
- `rotbound` puts the rotation-only ceiling for that placement at 444/480, so
  the placement itself cannot be rotated past 444.
- Starting *from* it, the search cannot find a 442 in ~10⁹ iterations.
- Best-of-N scaling on the measured distribution (mean 438.0, sd 1.54) gives
  P(single run ≥ 442) ≈ 0.5%. Best-of-100 would reach about 441.8 and
  best-of-1000 about 443 — i.e. roughly 40 core-hours for a coin flip.

The mean improvements here are real and worth having. Beating 441 needs a
different *neighbourhood*, not more of this one: the searcher's optima are
simultaneously optimal for a very wide family of moves.

## Note on noise

Runs are **not seed-reproducible**: the main loop is bounded by wall clock, so
iteration counts vary with machine load. Every A/B above was therefore repeated
over two independent seed batches, and even so individual comparisons are noisy
at about ±2 edges. Treat single-run differences as meaningless.
