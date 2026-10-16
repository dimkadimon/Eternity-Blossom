# Eternity-Blossom: design notes

How to use Blossom VI (minimum-weight perfect matching) on Eternity II, what it
can and cannot do, and why the answer to "isn't E2 just a matching problem?" is
more interesting than yes or no.

---

## 0. The honest summary

**Blossom cannot solve Eternity II, and no polynomial-time algorithm can.**
Eternity II is an edge-matching puzzle; the general problem is NP-complete
(Demaine & Demaine, *Jigsaw and edge-matching puzzles*, 2007), and this specific
instance has famously resisted every solver thrown at it since 2007. Minimum
weight perfect matching (MWPM) is solvable in polynomial time by Edmonds'
blossom algorithm. If E2 reduced to MWPM we would have P = NP.

What matching *does* give you is three things that no other polynomial-time tool
gives you as cleanly:

1. an **optimal polynomial-time move**: "given the board as it stands, what is
   the provably best set of non-interfering swaps to make?" — this is an MWPM
   problem, and it is *not* bipartite;
2. an **admissible lower bound** on the number of mismatched edges that any
   completion of a partial board must suffer, again via MWPM on a non-bipartite
   graph, usable to prune branch & bound;
3. an **optimal reassignment operator**: the best permutation of a pool of
   pieces over a set of cells, which is MWPM again.

This repository implements all three, plugs them into a late-acceptance /
simulated-annealing search, and measures what each one is actually worth. The
measurements are in [experiments/RESULTS.md](experiments/RESULTS.md); they are
reported honestly, including the places where the matching machinery does *not*
pay for itself.

---

## 1. Model

### 1.1 Encoding

A piece is four colours, clockwise from North: `c[0]=N, c[1]=E, c[2]=S, c[3]=W`.
Colour `0` is grey, the border colour. Rotating a piece clockwise by `r` quarter
turns puts the colour `c[(s - r) mod 4]` on side `s`. Pieces have a rotational
symmetry period of 1, 2 or 4, and the code exploits that so it never evaluates
two identical rotations.

The authentic 16×16 instance (`data/e2pieces.txt`) is verified on load:

```
pieces=256 board=16x16 internal_edges=480 border_slots=64 colours=23
grey_count=64 (needs 64)
corners=4 edges=56 interior=196
non_grey_colour_parity_even=yes
```

The 64 grey edges exactly match the 64 border slots, which is a strong
structural hint that the file is genuine: 4 pieces carry two adjacent grey edges
(the corners) and 56 carry one (the perimeter). Every non-grey colour occurs an
even number of times, which is necessary — internal adjacencies pair stubs, so
each colour class must have even cardinality.

### 1.2 Objective

The board is always **full**: every cell holds one piece in one of four
rotations, so every move is a permutation of the pieces. There are `4·W·H`
**slots** (cell, side). Each slot has

|            | internal slot                    | border slot        |
|------------|----------------------------------|--------------------|
| colour     | the colour facing out of it      | ditto              |
| target     | the colour facing it from across | `kGray`            |
| weight     | 1                                | `W = 2·wBorderRel` |

and the objective is the total weight of violated slots:

```
cost = Σ_slots weight · [colour ≠ target]
     = 2 · (mismatched internal edges) + W · (bad border slots)
```

The factor 2 is because an adjacency is seen from both endpoints: both of its
slots report the same violation. A border slot belongs to exactly one cell. That
asymmetry is the one genuinely subtle part of the implementation — see the
comment at the top of `include/e2/board.hpp`. A perfect solution has `cost = 0`.

Scoring convention: **the record score is the number of the 480 internal
adjacencies whose two colours agree.** A board is a *solution* only if it scores
480 **and** has a clean grey border. The search therefore weights a border
violation `wBorderRel = 8` times as much as one mismatched internal edge; the
reported `matched / 480` is always recomputed from scratch by `CheckBoard`.

---

## 2. Where matching is *not* the answer

It is worth being precise about this, because it is the obvious first idea and
it fails in an instructive way.

**The stub graph.** Give every piece four "stub" vertices, one per side. Join two
stubs when their colours agree. A perfect matching of this graph pairs every stub
with a compatible stub. Every solved board induces such a matching (pair the
stubs that face each other), so:

> if the stub graph has no perfect matching, the puzzle has no solution.

That is a polynomial-time **necessary** condition. It is also completely
powerless here. Running it at the root:

```
$ ./build/eternity-blossom bound --pieces data/e2pieces.txt
  vertices 1088  edges 587776
  lower bound on mismatches: 0
  matching time 0.069s
```

The bound is 0: the relaxation proves nothing. The reason is that a perfect
matching of stubs is a much weaker object than a board. It knows nothing about
geometry (which stub is North), nothing about cells (256 pieces must occupy 256
distinct cells), and nothing about the fact that a piece's four stubs are welded
together in a fixed cyclic order. The matching happily pairs the North stub of
piece 7 with the East stub of piece 200. **The hardness of Eternity II lives
entirely in that welding constraint**, which a matching relaxation throws away.

The important consequence, and the design principle of this whole project:

> A matching relaxation is only useful when it is applied to a sub-problem
> whose *other* constraints are already fixed by the surrounding search.

Everything below follows from that.

---

## 3. Operator 1: the optimal packet of disjoint swaps (non-bipartite)

This is the operator that most deserves the name "Blossom for Eternity".

### 3.1 The construction

Take the board as it stands. Swapping the pieces on two cells `u`, `v` (both
re-oriented optimally) changes the objective by an exactly computable amount
`δ(u,v)`. Now consider a **set** of swaps on pairwise disjoint cells. If, in
addition, no two of the swapped cells are grid neighbours, then the swaps do not
interact — each one touches four slots belonging to cells that no other swap in
the set touches — and the total change is exactly the **sum** of the individual
`δ`s.

So: build a graph whose vertices are cells and whose edges are candidate swaps
with weight `−δ(u,v)` (only positive-gain swaps are worth including). Choosing
the best possible packet of non-interacting swaps is **maximum weight matching**
in that graph.

### 3.2 Why it is not bipartite

The graph contains an edge `(u,v)` only when `u` and `v` are **not** grid
neighbours. It is the *complement of the 16×16 grid graph*.

The grid graph is bipartite (checkerboard), but the complement of a bipartite
graph is not. Concretely, take three cells `a`, `b`, `c` with `a—b` and `b—c`
grid edges and `a—c` not a grid edge (e.g. `(0,0)`, `(1,0)`, `(1,1)` translated
anywhere). Then `a—b` and `b—c` are forbidden as simultaneous swaps but `a—c` is
fine, and this pattern repeats everywhere: the swap graph is full of odd cycles.
Edmonds' blossom contraction — contracting an odd cycle into a single
supervertex — is exactly the machinery needed for that, and bipartite solvers
(Hungarian, min-cost flow, auction) cannot express the problem at all.

Note the direction of the difficulty: it is the *independence* requirement (two
swaps may not interfere) that breaks bipartiteness. Drop it and you are back to
an assignment problem.

### 3.3 The complement-of-grid subtlety

A matching guarantees that no two chosen swaps share a cell. It does **not**
guarantee that no two chosen swaps involve neighbouring cells, which is the
condition that makes the gains additive. There are two ways to deal with this
and the code does both:

* the matching is computed on the surrogate objective (sum of individual gains),
  and
* the resulting packet is filtered — swaps sorted by decreasing gain, a swap
  dropped if either of its cells is a grid neighbour of a cell already accepted —
  after which the **true** objective change is measured on the real board.

The pass is a *proposer*: if the true change is not an improvement it is rolled
back. Optimality of the matching therefore never compromises correctness of the
search; it only makes the proposal good. In practice (see experiments) roughly
60–65 % of the predicted gain survives contact with the real board, which is a
useful number: it says the surrogate is informative but not tight.

### 3.4 Reduction used

Blossom VI solves **minimum-weight perfect** matching. To get a maximum-weight
matching of arbitrary cardinality we use the standard dummy-clique reduction
(implemented in `src/matching.cpp`): add a dummy vertex `dᵢ` per real vertex `i`,
edges `(i, dᵢ)` of weight 0 (meaning "i stays unmatched") and a clique on the
dummies of weight 0 (so leftover dummies can absorb each other). `2n` vertices,
perfect matchings of the constructed graph ↔ matchings of the original, and the
weight is negated.

```
$ ./build/eternity-blossom solve ... # see experiments/RESULTS.md
blossom passes: 18171 (improved 9005, reverted 9166)
blossom gain  : predicted 203576, measured 126140
```

---

## 4. Operator 2: the stub-matching lower bound (non-bipartite)

### 4.1 The construction

Suppose part of the board is fixed and a set `R` of cells is still open, with a
pool `P` of pieces (`|P| = |R|`). Build a graph:

* **stub vertices** — four per piece of `P`, one per side, carrying that side's
  colour;
* **demand vertices** — one per slot of `R` that is already determined: the slot
  faces a cell that is already placed (demand = that cell's colour) or it faces
  the border (demand = grey);
* **stub–stub edges** between stubs of *different* pieces (a piece never touches
  itself), cost 0 if the colours agree, 1 otherwise;
* **stub–demand edges**, same costs;
* **no demand–demand edges** — two determined slots never face each other.

A legal completion of `R` induces a perfect matching of this graph: pair the
stubs that end up facing each other, pair each boundary stub with its demand.
Its cost is exactly the number of violated slots in and around `R`. Therefore

> min-cost perfect matching ≤ the number of violations of *any* completion

which is an **admissible lower bound** for branch & bound. And if the graph has
no perfect matching at all, the region is unsolvable — the relaxation doubles as
a feasibility oracle.

Vertex parity works out: `4|R| = 2·(internal pairs) + |demands|`, so the number
of demand vertices is always even and the graph always has an even number of
vertices.

### 4.2 Why it is not bipartite

Because pieces may be rotated freely, **a stub carries no direction**. Any stub
may face any way round, so there is no "North stubs must pair with South stubs"
typing. The compatibility graph therefore contains triangles — three pieces that
each have a red edge form a 3-cycle of zero-cost pairings — and no 2-colouring
exists. Blossom is required.

This is a nice illustration of the general principle: it is precisely the
*freedom to rotate* that destroys the bipartiteness. If rotations were fixed in
advance, the graph would split into N/S and E/W classes and a min-cost flow
solver would do.

### 4.3 How much is it worth?

Honestly: not much, yet. At the root it is 0 (§2). Inside a branch & bound over
a small region it does fire, but on the 16×16 instance the pruning it buys does
not repay its cost — Blossom VI takes a few hundred microseconds per call, and a
branch & bound that calls it at every node is orders of magnitude slower than
the local search it is trying to help. `RegionReoptimize` is in the repository
with the bound wired in and instrumented (`--region`), the numbers are reported
in the experiments file, and the conclusion there is a negative one. We keep it
because the bound is *correct* (it is exercised by `selftest`, which checks
admissibility against brute force on random 2×2 instances) and because on
smaller instances — 4×4, 5×5 — it is a different story.

Where it should pay: an exact solver that fixes most of the board and then has
to decide whether the last two or three rows can be completed. That is exactly
the regime where the relaxation is tight, because the demand vertices dominate
the stub vertices.

---

## 5. Operator 3: optimal reassignment (bipartite — but still matching)

Take a set `S` of cells and the pool `P` of pieces sitting on them, and ask for
the best **permutation** of `P` over `S` — not just a product of disjoint swaps.
Every cell is scored against the board as it stands:

```
cost(cell c, piece p) = min over rotations r of (violated slots of c with p in rotation r)
```

A perfect matching between the `|S|` cell vertices and the `|S|` piece vertices
is exactly an assignment of one piece to each cell, so this is MWPM again, this
time on a bipartite graph.

This operator is complementary to Operator 1:

|                     | swap packet (§3)                  | reassignment (§5)         |
|---------------------|-----------------------------------|---------------------------|
| reach               | disjoint transpositions only      | every permutation         |
| gain accuracy       | exact (neighbours don't move)     | surrogate (all cells move)|
| graph               | non-bipartite                     | bipartite                 |

Because all the cells move at once, each cell's score is computed against
neighbours that are about to change, so the predicted gain is only a surrogate
and the pass is again rolled back if the measured change is not an improvement.
Honest note: this sub-problem is bipartite, so a Hungarian / Jonker-Volgenant
solver would be the textbook choice and would be faster. It is solved with the
same Blossom VI engine here so the project has exactly one matching dependency;
Blossom VI handles bipartite graphs as the no-odd-cycle special case.

---

## 6. The search around the operators

The operators are proposers; the search around them is ordinary (and the tuning
is recorded in `experiments/RESULTS.md`):

* **Initialisation.** Perimeter-first greedy: the four two-grey pieces to the
  corners and the 56 one-grey pieces to the perimeter with their grey edge(s)
  facing outward, then the interior filled greedily row by row, each cell taking
  the remaining (piece, rotation) that minimises violated weight against the
  already-placed neighbours. A little noise (5 %, top-4 pick) gives restarts
  diversity. This matters a lot: it starts from a legal border, so the search
  never spends budget relearning that grey goes outside.
* **Move generation.** A "best of K proposals" scheme. Each iteration samples up
  to `K` candidate moves and acts on the most promising one; `K = 1` is plain
  random-proposal hill climbing, `K = 16` is steepest descent over a sampled
  neighbourhood. This single knob was worth **+8 matched edges** (424 → 432) —
  see the experiments.
* **Aimed moves.** 85–100 % of proposals are *aimed*: pick a violated internal
  edge, read off the colour that one of its slots is asking for, look that
  colour up in an index of "cells whose piece carries colour c", and propose
  bringing one of those pieces in. Uniformly random cell pairs are much weaker.
* **Acceptance.** Late-acceptance hill climbing (history 200) or simulated
  annealing with geometric cooling; both are available and they perform within
  noise of each other once `K` is tuned.
* **Restart / perturbation.** After a long stall, apply a handful of random
  swaps and refill the history.
* **Parallelism.** Independent searches with different seeds, best board wins.

### 6.1 Incremental scoring

The whole thing runs at several million moves per second because nothing is
recomputed. `Board` maintains `col`, `target`, `cellCost`, `cost` and `nbbad`
incrementally; `deltaSwap` evaluates a candidate swap without touching state,
and a precomputed `protCol[piece][rotation][side]` table keeps rotation
arithmetic out of the inner loop. `selftest` verifies the fast delta against the
exact delta and both against a full rebuild, 20 000 times over.

---

## 7. Complexity

| piece                             | cost                                   |
|-----------------------------------|----------------------------------------|
| incremental swap delta            | O(1) — 8 slot evaluations              |
| best-of-K proposal                | O(K)                                   |
| swap-packet operator              | O(\|C\|²) evaluation + MWPM on 2\|C\| vertices |
| reassignment operator             | O(\|S\|²) evaluation + MWPM on 2\|S\| vertices |
| stub bound                        | MWPM on 4\|P\|+\|D\| vertices, O(\|P\|²) edges |

`|C|` ≤ 96 candidate cells and `|S|` ≤ 64 cells by default, which keeps the
matching calls in the sub-10 ms range — Blossom VI solves a 1088-vertex,
587 776-edge graph in 69 ms here, so these are small instances for it.

---

## 8. Results, and how far this is from the record

Current best with this code: **441 / 480** (300 s, 2 threads; verified by two
independent checkers, clean grey border, every piece used exactly once). Typical
single-core runs reach 434/480 in 60 s. Full tables in `experiments/RESULTS.md`.

The record is **470/480**, held by solvers that have had years of tuning and
longer runs on bigger machines. Being straight about the gap: this project does
not beat it, and the honest read of the experiments is that the matching
operators are worth a few matched edges, not tens. The plateau at ~432 is a
property of the *neighbourhood*, not of the matching machinery — the swap
neighbourhood saturates, and what is missing is a genuinely different move (row
or column chain moves, ejection chains of depth > 1, or a SAT/CP model over a
sub-board), not a better matching solver.

What is here and is genuinely new, as far as we know:

* the identification of the "optimal packet of disjoint swaps" as a
  **non-bipartite** matching problem on the complement of the grid graph, with
  Blossom used to solve it to proven optimality;
* the stub-matching admissible bound, with the observation that it is the
  freedom to rotate (not the geometry) that makes it non-bipartite;
* an implementation of both on top of Blossom VI, with self-tests that check the
  matching engine against brute force and the bound against exhaustive search.

### Roadmap, most promising first

1. **Depth-first ejection chains.** The 3-cycle is implemented and currently
   *hurts* (see experiments) because its third leg is chosen at random; choosing
   it greedily — "where does the ejected piece actually want to go?" — is the
   obvious fix and the standard next step for this family of problems.
2. **Exact sub-board solving with the stub bound.** Rows 1..k fixed, solve the
   last rows exactly. This is where the bound in §4 becomes tight and where a
   branch & bound can actually beat local search.
3. **Hungarian instead of Blossom for the bipartite operators** (§5), freeing
   the matching budget for the non-bipartite ones.
4. **SAT/CP hybrid.** Encode a sub-board (say 6×16) as SAT with the rest of the
   board fixed, solve to optimality, iterate. This is the family of techniques
   behind most of the published E2 results.
5. **Port the hot loop to a bitboard/SIMD delta** and run a large parallel
   portfolio for days; the record scores come from long runs.

---

## 9. Licensing

Everything in this repository is **MIT**, except the optional dependency Blossom
VI (`github.com/Paul566/blossom-vi`, arXiv:2604.20351), which is **GPL-3.0** and
is *not* redistributed here — CMake clones it at configure time. If you build
with `-DE2_USE_BLOSSOM_VI=ON` (the default) the resulting binary is a combined
work and must be distributed under the GPL-3.0; the sources here stay MIT. Build
with `-DE2_USE_BLOSSOM_VI=OFF` for a pure-MIT build (correct but much slower:
exact subset-DP for `n ≤ 18`, greedy + 2-opt above that).

If you use Blossom VI, cite the paper:

```
@article{arkhipov2026blossomvi,
  title   = {Blossom {VI}: A Practical Minimum Weight Perfect Matching Algorithm},
  author  = {Arkhipov, Pavel and Kolmogorov, Vladimir},
  journal = {arXiv:2604.20351},
  year    = {2026},
}
```
