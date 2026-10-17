# Dissection reassignment: an exact bipartite operator, and why it finds nothing to do

This is the write-up of the "precompute 1000 dissections and reassign with Hungarian"
experiment. It is a **negative result**, and a clean one, so it is worth recording
carefully rather than burying.

Reproduce with:

```
./build/eternity-blossom dissect --board results/best_441.txt \
    --dissections 1000 --mixed 50 --time 180
./build/eternity-blossom dissect --board <weak-board> --dissections 40 --mixed 8 --stats
```

## What was built

`include/e2/dissect.hpp`, `src/dissect.cpp`, plus the `dissect` command.

A **dissection** splits the board into connected groups. Five families:

| constructor | groups |
|---|---|
| `MakeDominoDissection(pz, seed)` | random domino tiling, seed-parameterised |
| `MakeMixedDissection(pz, seed)`  | L-trominoes, Z/S-tetrominoes, dominoes, monominoes |
| `MakeRowStripDissection(pz)`     | whole rows |
| `MakeColumnStripDissection(pz)`  | whole columns |
| `MakeSquareDissection(pz, s)`    | s×s blocks, with phase offsets |

The move itself: pick an **independent set** of groups — no two share an edge — freeze
everything else, and reassign the chosen groups to the best cells with the best rotations
by an exact assignment.

**Independence is what makes it exact.** If no two selected groups touch, the objective
splits into a sum of per-slot terms, each depending only on the group placed there and on
frozen neighbours. The cost is therefore *additive* over the set, and one assignment
problem minimises it exactly. Groups on one side, slots on the other: **bipartite**, so
Hungarian, and Blossom is not needed here.

Two details that turned out to matter:

* **One Hungarian per shape class.** A rotation never changes a shape, so a group can
  only occupy a slot of its own shape. In a homogeneous dissection (all dominoes) that
  is automatic; in a mixed L/Z dissection it is not, and folding all classes into one
  matrix fills it with infeasible entries whose "optimum" is a sentinel.
* **Canonicalisation.** `dst[]`/`inv[]` are permutations expressed in canonical offset
  indices, so every group's cells must be listed in canonical order. Several
  constructors handed over shapes that were neither normalised nor sorted.

## Result: zero. Exactly zero.

On the 441 board:

```
library: 1057 dissections built in 0.56 s
sweep 1: gain 0, cost 78 (441/480)  t=0.63s
converged: full sweep with no improvement
start cost 78 -> best cost 78  (improvement 0)
```

Same on a 424 board, a 423, a 419 and a 418. Not "a little" — **identically zero**,
across 1057 dissections, in well under a second.

## Ruling out a bug

A zero result this clean demands that the *solver* be checked, not just the plumbing.
Self-test `[7/7]` does three things:

1. **Every dissection is a partition** into connected groups, each with four valid
   rotations.
2. **Predicted gain == measured change, to the unit.** The whole claim of `dissect.hpp`
   is that independence makes the cost additive, so the Hungarian optimum is the *true*
   optimum. Any discrepancy is a bug. There is none.
3. **`HungarianMin` is optimal**, checked against brute-force enumeration of all n!
   permutations on random matrices up to 7×7. This is the load-bearing check: the claim
   "no improving reassignment exists" is only as good as the guarantee that the
   assignment solver really returns the minimum.

And the operator demonstrably *works* — on a randomised board:

```
dissection          |S|   predicted   measured
domino#9             39          90         90
domino#22            42          88         88
...
mean |S| 38.3, positive predicted gain: 30/30, mean gain 81.9, best 94
```

From a random start the whole pipeline takes the board from **26/480 to 323/480 in 1.2 s**.
The independent sets are large (|S| ≈ 38) and the gains are real and exactly as predicted.

## So why zero on real boards?

Because a converged board is already a **fixed point of this exact neighbourhood**, and
so is every board the main searcher produces. The searcher's own assignment passes
("assign passes: 1892, improved 296, measured gain 3646" in a 45 s run) exhaust the same
territory long before `dissect` is called.

The reason is structural. The identity assignment is always feasible, so the optimum is
never worse than doing nothing. For the Hungarian to find an improvement it needs a
*cycle* of moves whose total is negative — and any such cycle of length 2 is a pairwise
swap, which the searcher has already driven to exhaustion. Longer cycles, in these
experiments, never paid.

Adding perturbation did not help either. `--kick N` implements iterated local search
(kick with N random transpositions, re-converge exactly, keep only if better):

| kick | result on the 424 board |
|---|---|
| 2   | 3 479 kicks, best unchanged at 424 |
| 12  | 2 528 kicks, best unchanged at 424 |

A 12-swap kick takes the board to 592 and re-convergence only reaches 392 — the exact
operator is a strong *local* improver and a poor *global* one.

## What it is good for

Not nothing:

* **A fast, exact polisher for mid-quality boards.** 26 → 323 in 1.2 s is a real
  capability; it just plateaus well below 441.
* **A rigorous converged-ness certificate.** "Zero gain across 1057 dissections" is a
  strong statement about a board: it is optimal with respect to every one of these
  exponentially large neighbourhoods simultaneously. That is genuinely useful
  diagnostics, and it costs 0.6 s.
* **The bipartite half of the story**, sitting next to the non-bipartite swap operator in
  `coarse.cpp`. The contrast is the interesting part: reassigning an independent set is
  an *assignment* problem (Hungarian); swapping pairs of blocks is a *matching* problem
  on the complement graph (Blossom). See `experiments/blossom-2x3-example.html`.

## Honest summary

The operator is correct, exact, fast, and finds nothing to do on boards that have already
been optimised. That is a property of the boards, not of the operator — and it is the
reason the interesting non-bipartite work is elsewhere.
