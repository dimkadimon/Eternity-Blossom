# Three more ideas: coarse-graining, rotation-only bounds, exact sub-boards

Measured on the real 256-piece Eternity II set, 16x16, 480 internal edges.
Baseline for every comparison is the tuned search of `RESULTS.md`
(`--sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256`), which scores
**434-435 / 480 in 60 s on 2 threads** and **441 / 480 in 300 s**.

Every operator below is checked against brute force by
`eternity-blossom selftest` (tests 4-6 of 6). Nothing here is heuristic: each
pass either lowers the true `Board::cost` or is rolled back.

---

## 1. Coarse-graining: 2x2 / 4x4 / 8x8 blocks and domino dissections

### What was built

A **rigid block motion** moves a group of cells as one object. Two families:

* **Square blocks.** Dissect into 2x2 (64 super-tiles on an 8x8 board), 4x4 (16)
  or 8x8 (4). Two moves become available:
  * *block rotate* - turn the block by 90/180/270 degrees about its centre;
  * *block swap* - translate two blocks onto each other.

  The block's internal structure is a symmetry of the block, so a rotation
  carries every internal adjacency onto another internal adjacency **bringing the
  same pair of colours with it**. Individual internal edges move (top -> right ->
  bottom -> left for a quarter turn) but **how many of them match does not
  change**. A 2x2 block keeps the status of all 4 of its internal edges, a 4x4
  keeps 24, an 8x8 keeps 112. Only the 4s boundary unit-edges can change the
  objective, so the delta costs 4s slot lookups and is exact.

* **Dominoes (1x2).** There is no canonical domino dissection of a 16x16 board -
  there are astronomically many - and each one induces a **different move set**.
  A flip exchanges the domino's two cells and half-turns both pieces, preserving
  the domino's internal edge and re-exposing its six boundary edges. Sampling a
  fresh dissection (by running the standard connected 2x2-flip Markov chain from
  the all-horizontal tiling) hands the search 128 fresh large moves on the same
  board state.

### The non-bipartite part

A packet of block swaps may only be applied together if no two blocks involved
touch, so the legal pairs are the **non-adjacent** pairs of super-tiles: the
complement of the coarse adjacency graph. On the 8x8 block board that complement
contains triangles (three blocks, no two sharing a unit edge), so the optimal
packet is a maximum-weight matching on a **non-bipartite** graph - the identical
Blossom VI call to the cell-level operator, one level up. With a domino tiling
(128 vertices) the graph is bigger and further from bipartite still.

### Measured (60 s, 2 threads, seed 5; baseline 434)

| configuration | matched | what the operator did |
|---|---|---|
| baseline | **434** | - |
| + block rotate sweep | 433 | 7275 sweeps, 276 rotations taken, total gain 602 |
| + block swap packet | 432 | 1444 passes, **9 improved**, measured gain 18 |
| + strip rotation DP | 433 | 3564 sweeps, 1410 strips improved, gain 3130 |
| block rotations as the perturbation | 431 | (3 seeds: 430 / 432 / 431) |
| sub-board B&B 3x3, 3 seeds | 434 / 435 / 432 | mean 433.7 vs baseline 434.3 |

**Honest reading: all of these are neutral or slightly harmful.** The operators
*work* - they fire, they are exact, they move the objective - but the search's
own move set already finds what they find, and the time they spend is time not
spent on the main loop.

The block-swap packet is the most telling: 1444 Blossom passes produced 9
improvements. The optimal packet of non-interacting chunk swaps is a genuinely
large move, and at this point in the search there is almost nothing for it to do.

The clearest negative is using chunk moves as the **perturbation**: 431.0 vs
434.3 over three seeds. Random block rotations preserve too much structure - they
are a *weaker* diversification than 24 random cell swaps, not a stronger one.

---

## 2. Rotation-only: exact strip DP and an upper bound  ← the winner

Freeze which piece sits on which cell and let only the rotations vary. That is a
pairwise CSP: 256 variables of domain 4, one constraint per internal edge. The
constraint graph is the grid, so it is NP-hard in general - but it has enough
structure for two exact things.

### (a) Exact strip optimisation (a move)

`h` consecutive rows have pathwidth `h`, so the sub-problem restricted to them is
solved **exactly** by a DP sweeping the columns with the 4^h rotations of the
current column as state. h=2 is 16 states, h=3 is 64, h=4 is 256 - all cheap.
One pass re-solves 32 (or 48, or 64) rotation variables simultaneously and
optimally, and never worsens the board because the current rotations are one of
the DP's feasible states.

### (b) An upper bound (the oracle)

Partition the 480 internal edges into groups, solve each group exactly, and sum:
relaxing the constraints *between* groups can only help, so the sum is an upper
bound on what **any** rotation assignment can achieve. With strips of `h` rows:

* every strip contributes the exact optimum over **its own** internal edges (the
  DP above, told to ignore edges leaving the strip), and
* the seams between consecutive strips contribute their exact optimum **for
  free**: seam edges are vertex-disjoint, so they do not interact and the optimum
  is the sum of the per-edge optima.

Those two families partition all 480 edges, so

```
matched  <=  480 - ( sum of strip minima + sum of seam minima )
```

is rigorous. If it is below 480, **this placement cannot be completed by rotating
alone** - which makes it a placement filter: stop polishing rotations and go and
move some pieces.

Cost: 4 ms at h=4.

### Results

| placement | current | per-edge bound | star bound (halved) | **strip bound (h=4)** |
|---|---|---|---|---|
| random | 20 | 223 | 161 | **145** |
| 60 s search board | 436 | - | - | **439** |
| best board (441) | 441 | 465 | 446 | **444** |

**On the 441 board rotations alone cannot exceed 444** - at least 36 edges must
stay broken no matter how the pieces are turned. On a random placement the
ceiling is 145.

Two things worth noting about the bound hierarchy:

* The **per-edge** bound (an edge no rotation of either piece can ever match) is
  the weakest by far - 465 on the 441 board. Individual edges are almost always
  matchable; it is the *coupling* between the four edges of a piece that binds.
* The **star** bound - the idea as originally posed, "optimise one tile together
  with its neighbours" - lands at 446 and is a real bound, but it **counts every
  internal edge twice** (once from each endpoint). The raw sum is 892; it has to
  be halved before it can be compared with 480. That halving is what makes it
  bite: 892 > 480 looks like no information at all, 446 < 480 is a proof.

### Polish: the operators as an independent optimiser

`eternity-blossom polish` runs every exact operator on an existing board until
nothing moves. On both the 441 board and a fresh 436 board the gain was **0**:

```
before   : 441/480   ceiling: rotation alone cannot exceed 444/480
after    : 441/480   strips applied 0, blocks 0, sub-boards solved 0
```

The boards coming out of the search are already at a fixed point of the strip DP,
of every rigid block rotation and domino flip, of the optimal block-swap packet,
and of exact 3x3 sub-board re-solves. That is a negative result for the operators
and a positive one about the search: its local optima are optimal with respect to
a much larger neighbourhood than the neighbourhood it searches.

---

## 3. Exact sub-board solving with the stub bound

A branch and bound that re-solves a rectangular region to optimality, placing the
region's own pieces back onto the region. Every internal edge of the region and
every edge on its boundary is accounted for; the surroundings are frozen, so
boundary edges are unary costs.

### How large a region can be proved optimal

| region | cells | proved optimal | nodes/solve |
|---|---|---|---|
| 2x2 | 4 | 100% | 9 |
| 2x3 | 6 | 100% | 98 |
| 3x3 | 9 | 100% | 6,320 |
| 3x4 | 12 | 97.6% | 104,742 |

For a 3x3 region the brute-force space is 9! x 4^9 = 3.6e11; the search visits a
few thousand nodes. **3x3 is reliable, 3x4 is mostly reliable, 4x4 is out of
reach.**

### Which lower bound is worth paying for

`eternity-blossom bench-subsolve` solves the **same** regions in the **same**
board state under each combination of bounds, so the comparison is controlled.
300 random 3x3 regions of the 441 board:

| bounds | nodes/solve | seconds | |
|---|---|---|---|
| all three | **494** | 0.44 | |
| - stub (Blossom VI) | 662 | **0.14** | 3.2x faster with the stub bound OFF |
| - assignment (Hungarian) | 2,549 | 7.13 | 16x slower: this is the one that matters |
| - edge | 494 | 0.53 | |
| stub bound only | 2,571 | 11.84 | |
| no bounds | 11,955 | 0.58 | |

At 12 cells the picture is the same, only louder: all bounds 111,862 nodes /
13.2 s; without the assignment bound 287,658 nodes / 116.8 s and it fails to
prove optimality inside a 5M-node cap; without any bounds 562,996 nodes and it
also fails.

Every configuration finds the **identical** optimum in every region, which is the
admissibility check: no bound ever pruned the answer away.

### What the stub bound is actually worth

The stub bound works, and it is the non-bipartite one - give every remaining
piece four directionless stubs, charge 1 whenever a matched pair disagrees, and
the minimum-weight perfect matching is a lower bound on the mismatches. Pieces
rotate freely, so a stub has no N/S vs E/W typing, three red-edged pieces form a
triangle, and blossoms are unavoidable.

It prunes: 44% of calls at 3x3 cause a cut. But **it loses on time**. Each call
is ~2 us and a branch-and-bound node is ~0.16 us, so a call has to save ~12
nodes to pay for itself. Near the leaves there is nothing left to save, and that
is exactly where the default policy was calling it.

Sweeping the depth at which it fires (3x3 regions, `rem` = cells still free):

| stub bound called when | nodes/solve | seconds |
|---|---|---|
| 0 <= rem <= 9 (everywhere) | 494 | 0.45 |
| 4 <= rem <= 9 | 508 | 0.40 |
| 6 <= rem <= 9 | 569 | 0.29 |
| 7 <= rem <= 9 | 629 | 0.22 |
| **8 <= rem <= 9 (top 2 levels)** | **651** | **0.18** |
| not called at all | 662 | 0.19 |

So the answer to "does the stub bound help?" is: **it depends entirely on where
you call it.** Everywhere: a 2.3x net loss. Only where a prune still kills a
large subtree: a 6% net win. That is now the default (`stubAutoShallow`).

> **A correctness trap found by testing the MIT-only build.** The repository is
> MIT, but Blossom VI is GPL-3.0, so `-DE2_USE_BLOSSOM_VI=OFF` builds against a
> built-in fallback matcher — an exact subset DP up to 18 vertices and a greedy +
> 2-opt heuristic above that. That heuristic is *not* guaranteed optimal, and the
> stub instances for a 2x3 region already exceed 18 vertices. The result was a
> bound that was not a bound: the branch and bound happily pruned the true
> optimum (it reported 12 where brute force says 8). Fixed by
> `MatchingEngineIsExact()`; `StubBoundResult` now carries a `heuristic` flag and
> the branch and bound refuses to prune on it. Worth stating plainly: **a
> heuristic matching engine cannot be used as an admissible bound**, and the
> failure is silent — the answers look fine, they are just not optimal.

The honest conclusion is that in this branch and bound the **bipartite**
assignment bound is the load-bearing one (4-9x), the cheap edge bound earns its
keep, and the **non-bipartite stub bound is the weakest of the three** - not
because the relaxation is wrong but because it is too loose to pay for a Blossom
call at this node cost. It would need to prune ~12 nodes per call and prunes
~0.25. Making it pay means making it *stronger* (the obvious gap: it lets a
piece's four sides be chosen independently, when in fact they must come from one
rotation), not cheaper.

---

## Summary

| idea | verdict |
|---|---|
| coarse blocks + dominoes as moves | exact, verified, **neutral to harmful** on score |
| Blossom packet on the coarse board | non-bipartite and correct; 1444 passes -> 9 improvements |
| exact strip rotation DP | exact and fast; **0 improvement** on search output |
| **rotation-only upper bound** | **strong and useful**: 444 on the 441 board, 145 on random, 4 ms |
| exact sub-board B&B | **exact to 9 cells (100%), 12 cells (97.6%)**; neutral on score |
| stub bound inside the B&B | admissible, prunes 44%, **net loss unless called shallow** |

## Long runs: what survives at 420 s

Same seed (21), 2 threads, 420 s, so the three runs are directly comparable:

| configuration | matched | iterations |
|---|---|---|
| baseline only (control) | 437 | 480.0 M |
| baseline **+ sub-board B&B (3x3)** | **439** | 339.3 M |
| baseline + everything (coarse, strips, subsolve, dominoes) | 434 | 339.7 M |

So at a longer horizon the sub-board branch and bound is worth about **+2**,
whereas at 60 s it was indistinguishable from noise. Everything piled on
together costs **3**: the coarse operators are doing real work (the third run
took 1012 block rotations, 197 block swaps and 2956 strip re-solves for a
combined 3486 units of objective improvement) but that work does not convert
into a better final board.

Best board overall is still **441 / 480** (`results/best_441.txt`).

## The one idea that produced something neither the search nor any published E2
technique I know of has is the **rotation upper bound**: a rigorous,
polynomial-time ceiling on what a given placement can achieve, computed by
partitioning the edge set into exactly-solvable groups. It turns "this placement
scores 441" into "this placement can never score better than 444".
