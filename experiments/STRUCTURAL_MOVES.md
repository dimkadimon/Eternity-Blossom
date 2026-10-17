# Population search and T-join moves

Two structurally different ways of using the matching machinery, both driven by the
same observation recorded in `PLATEAU.md`: every exact repair operator in this
repository reports **zero** improvement on a board the searcher has converged to.
Both are implemented, self-tested and measured. Neither moves the record. The
negative results are the contribution.

## 1. Memetic / population layer (`pop` command)

`src/popsearch.cpp`. Independent runs seeded with `SearchOnce`, then tournament
selection → rectangular crossover → refine the child with `SearchOnce` → replace
the occupant only if better.

### The child-cost diagnostic

The layer reports each child's cost **before** refinement. This is the number that
made the whole thing legible:

| repair strategy | child cost (mean / best) | parent cost | final |
|---|---|---|---|
| dump displaced pieces into freed cells | 124 / 96 | 90 | 432 |
| place them by exact assignment (Hungarian) | **110 / 90** | 90 | **435** |

Dumping the pieces displaced by the transplant into the freed cells in arbitrary
order wrecks cells that had nothing to do with the transplant — that was the
destructive step, and it is exactly a bipartite assignment once you notice it.
Placing them by `HungarianMin` over "damage at this cell", minimising over the four
rotations, narrows the parent→child gap from 34 cost units to 20.

### Rectangle size matters a great deal

| regions | child mean | child best | final | improvements |
|---|---|---|---|---|
| 2..3 | 129 | 100 | 431 | 18 |
| 2..5 | 125 | 92 | **434** | 19 |
| 3..9 | 323 | 180 | 429 | 0 |

Large rectangles are catastrophic: children start 230 cost units worse than their
parents and never recover within the refinement budget.

### Refinement temperature

Refining children at the searcher's normal temperature (`t0 = 3.0`) makes crossover
worthless — the hot schedule accepts almost anything and discards exactly the
structure the child inherited. A cold schedule (`breedT0 = 0.30`, `breedT1 = 0.05`)
is now the default and is a strict improvement (child mean 324 → 155).

### Does it beat restarts? No.

| config | seeds 1–4 @180 s | mean |
|---|---|---|
| control (`--no-crossover`) | 435, 432, 434, 434 | **433.75** |
| crossover + Hungarian repair | 433, 432, 432, 433 | 432.50 |

Also tied at 420 s with long refinement (433.5 vs 432.5) and at breed=2 s /
many generations (431.7 vs 431.0).

**Why.** `SearchOnce` reaches ~430 from essentially any starting point — random,
parent, or crossover child. The starting board is nearly irrelevant at these
budgets, so the population degenerates into a restart loop, and crossover only adds
the cost of destroying two good boards to make one mediocre one. Crossover would
only pay if the local search were weak enough for a child's inheritance to survive
it, or strong enough that no restart could reach the same band; here it is exactly
in between.

## 2. T-join moves (`tjoin` command)

`src/tjoin.cpp`.

Let **T** be the set of unhappy cells. Find a minimum-weight **T-join** — a set of
edges whose odd-degree vertices are exactly T — by minimum-weight perfect matching
on the complete graph over T with weights = −(Manhattan distance). This is
non-bipartite and goes through Blossom VI directly. Take the union of shortest
paths between the matched pairs; edges covered twice cancel, and what survives is
the join. Walk it into chains, and rotate the pieces one step along each chain.

The point is group-theoretic: a cyclic shift of *k* pieces is a single *k*-cycle,
and every other operator here is a product of disjoint transpositions. A *k*-cycle
is not expressible as any product of disjoint transpositions, so this explores a
region of the permutation group that all the swap/assignment machinery is blind to.
It is also the natural "defect repair" framing the T-join literature suggests, and
it connects defects that are far apart rather than only adjacent ones.

### Results

| board | before | after | gain |
|---|---|---|---|
| random | 29/480 | **135/480** | +212 |
| `results/best_441.txt` | 441 | 441 | **0** |
| converged 424 | 424 | 424 | **0** |

From random it is fast and effective (0.07 s for 200 rounds). On converged boards
it is exactly zero, in agreement with every other exact operator here.

### A real bug the self-test caught

A walk through a degree-4 vertex can revisit a cell. A chain containing a repeated
cell silently breaks the piece permutation — one piece duplicated, one lost — and
the operator would have reported a large phantom gain. Caught by the new self-test
(`[8/9]`, which checks both odd-degree parity on T and that the board stays a
permutation). Fixed by stopping the walk on revisit and by rejecting chains that
touch an already-moved cell.

## Reproduce

```sh
# memetic, crossover on
./build/eternity-blossom pop --time 180 --pop 8 --init 8 --breed 5 --breed-t0 0.3 \
  --reg-min 2 --reg-max 5 --seed 1
./build/eternity-blossom pop --time 180 --pop 8 --init 8 --breed 5 --no-crossover --seed 1  # control
./build/eternity-blossom pop ... --no-repair-assign          # the destructive variant

# T-join
./build/eternity-blossom tjoin --random --time 60 --rounds 5000 --out /tmp/tj.txt
./build/eternity-blossom tjoin --board results/best_441.txt --time 30 --rounds 5000
```

## Conclusion

Six structurally unrelated exact operators — assignment, blossom swap, LNS,
rotation strips, dissection reassignment, and now T-join cycles — all report
exactly zero on converged boards, and crossover cannot beat restarts. That is a
consistent, replicated statement about the landscape rather than a series of
implementation failures: the searcher's local optima are simultaneously optimal for
a very wide family of neighbourhoods, and beating 441 needs a method that changes
the global arrangement, not a better repair operator.

## 3. 2-factor cycle cover (`cyccover`)

`src/nmoves.cpp`. Choose a set S of cells, find a **minimum-weight 2-factor** on
the complete graph over S, and rotate the pieces one step around every cycle.
The 2-factor comes from **Tutte's gadget**: replace each vertex by two copies and
each edge by a K₂,₂; a perfect matching in the gadget is a perfect 2-matching
back in the original graph. Because the cell graph is complete (any piece may
move to any cell), it contains triangles, the gadget contains triangles, and the
instance handed to Blossom is genuinely non-bipartite.

Structurally different from the T-join: a T-join *connects* the unhappy cells
with paths and leaves everything else alone, whereas a 2-factor makes **every**
cell of S part of a cycle, so every cell of S gives up its piece.

| board | before | after | gain |
|---|---|---|---|
| random | 26 | **291** | +530 |
| random, `--set unhappy-hood` | 26 | **296** | +540 |
| random (T-join, for comparison) | 26 | 124 | +196 |
| SA-converged 423 / 426 / 441 | — | — | **0** |

From a random start the 2-factor is by far the strongest single move in the
repository (+530 vs +196 for the T-join), and it runs in milliseconds. On
converged boards: zero, like everything else.

### Two real bugs the self-test caught

Both would have produced confidently-reported phantom gains, and both were
found by test `[10/12]`, which asserts that every cell of S is covered exactly
once.

1. **Partial walks leaked cells.** The walk marked cells done and consumed edges
   as it went, so a walk that then failed its checks left its cells marked but
   never emitted a cycle — they vanished from the cover, and "rotating" the
   result silently dropped pieces. Fixed by making the walk transactional.
2. **2-cycles never closed.** The walk refused to traverse the edge it had
   arrived on, but in a 2-cycle that edge is the *only* one left, so the cycle
   aborted. A 2-cycle is a legitimate gadget outcome (both copies of one edge
   taken, i.e. a transposition). Fixed with a two-pass rule: prefer a fresh
   edge, fall back to the arrival edge only when stuck — the preference is what
   stops a triangle from closing prematurely as a 2-cycle.

## 4. Matching-guided ruin & recreate (`blast`)

`src/nmoves.cpp`. A maximum-weight matching pairs up the unhappy cells; the ruin
set is the union of the shortest paths between each pair, so the damage is
connected rather than scattered; then the pieces of the ruined cells are
reassigned to those cells **exactly**, by `HungarianMin`, each in its best
rotation. The matching that chooses the ruin set is non-bipartite and
cardinality-free; the repair is bipartite and provably optimal for that set.

| board | before | after | gain |
|---|---|---|---|
| random | 26 | **217** | +382 |
| SA-converged 423 / 426 / 441 | — | — | **0** |

**One bug worth recording:** the first version produced an empty ruin set on
every board. `MaxWeightMatching` is *cardinality-free*, so with all-negative
weights (`−distance`) its optimum is the **empty matching** — taking no pairs at
all scores 0, which beats any negative total. Fixed by shifting every weight by
(max distance + 1), so each is positive: taking more pairs is then always
worthwhile, and among matchings of a given size the short links win.

## 5. The odd-set certificate (`certify`) — and an uncomfortable finding

`certify` asks Blossom VI for its dual certificate (`compute_dual_certificate`)
on the graphs this project actually solves, and reports:

- **primal vs dual.** Equality is precisely the certificate of optimality.
- **live blossoms, how many carry a strictly positive dual, and the nesting
  depth.** A positive dual marks a genuine blossom — an odd set that Edmonds'
  algorithm had to contract. A bipartite solver produces no such thing.

Every instance this project feeds to Blossom is **non-bipartite** (complete
graphs on the unhappy cells, containing triangles), so assignment / Hungarian /
min-cost-flow genuinely cannot be used. But the certificate says something
sharper, and less comfortable:

```
T-join over unhappy cells (complete, -distance)
  primal -200  dual -200  CERTIFIED OPTIMAL
  live blossoms 40, with positive dual 0, nesting depth 0
  -> no blossom contracted on this instance
```

**On the board-derived instances, the number of blossoms contracted is zero.**
Same for the non-bipartite swap graph and for a T-join weighted by board shift
cost instead of distance, and on boards of every quality from random to 441.
The matching LP is already integral on these instances: no odd-set constraint is
ever violated, so the blossom-shrinking step — the part of Edmonds' algorithm
that has no bipartite analogue — is never exercised here.

That the detector is not simply broken is checked by a synthetic control
(`--synthetic`): on random small complete graphs with weights in [−20, 20],

```
synthetic: 3 of 4 random instances needed an odd-set contraction
```

So the honest statement of the project's position is narrower than "Blossom's
non-bipartite power is what makes this work":

- The graphs are **not bipartite**, so bipartite machinery is inapplicable —
  Blossom is required.
- But on these instances the **odd-set structure is vacuous**. An LP solver
  would return the same matching. What is being bought is speed and an
  optimality certificate, not the ability to handle odd sets.
- Finding an E2 sub-problem whose matching relaxation genuinely needs a blossom
  remains open. The `--synthetic` control is the tool for hunting one.

## Summary of every move, on SA-converged boards

| board | start | tjoin | cyccover | cyccover-hood | blast |
|---|---|---|---|---|---|
| SA 20 s | 423 | 0 | 0 | 0 | 0 |
| SA 60 s | 426 | 0 | 0 | 0 | 0 |
| SA 180 s | 426 | 0 | 0 | 0 | 0 |
| best | 441 | 0 | 0 | 0 | 0 |

Nine structurally unrelated exact operators now agree: a converged board is a
simultaneous fixed point of all of them. The interesting open question is no
longer "which repair operator" but "what global structure changes the answer".
