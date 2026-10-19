# The 84-cell a/b labelling, measured

The proposal: fill the 172 `a` cells by search, then place the remaining 84
pieces into the `b` cells by Hungarian matching.

## The geometry

It is a **perfect independent set** — not one of the 84 `b` cells touches
another — so the disjointness that makes the assignment exactly optimal holds
here, exactly as the 64-cell `d` pattern did in `DISJOINT_HOLES.md`:

```
cells                 : 84 b, 172 a
edges at stake        : 307   (b-b 0, b-a 307)
sides facing the border: 29     (must come out grey)
edges the search owns : 173
b-cell neighbour counts: [(0, 84)]     <- every b cell has zero b neighbours
```

Sanity: 2·(b-b) + (b-a) + border = 0 + 307 + 29 = 336 = 4·84. ✓

307 edges at stake, against **16** for the eight clustered holes the real
pipeline uses, and 240 for the 64-cell `d` pattern.  This is the number that
decides everything, because of the rate measured repeatedly this session:

| who decides an edge | matched |
|---|---|
| the search (filled–filled) | ~98% |
| the fill (edges touching a hole) | ~46% |

## The two measurements

Punch the 84 `b` cells out of the 467 board and let `complete` refill them —
once with the pieces that belong there, and once with 84 arbitrary pieces
parked there first (which is what an `a` region built without regard for the
`b` cells actually leaves behind).

**A. The right 84 pieces left over — the ceiling for the plan:**

```
edges at stake    307  (hole-hole 0, hole-filled 307)   frozen: 167/173 already matched
bipartite fill    300/307  ->  board 467/480
branch and bound  300/307  ->  board 467/480  (4341 nodes, proved optimal)
```

So the `a` region alone determines 167 of its own 173 edges, and given the
right leftovers the assignment recovers the board exactly.  That is the best
the two-stage plan could ever do, and it is what you get only if the search
happens to reserve precisely the pieces the `b` cells need.

**B. 84 arbitrary pieces — what the plan actually gets:**

| trial | score |
|---|---|
| 1 | 254 / 480 |
| 2 | 249 / 480 |
| 3 | 235 / 480 |

Mean ~246.  The fill matched ~140 of 307, i.e. **46%** — the same rate seen on
the 16 real partial boards (44%) and on the 64-cell `d` pattern (47%).  It is a
property of handing edges to a fill that does not have the right pieces, not a
property of the pattern.

## Why it cannot work, in one line

The search would own 173 edges and match ~170 of them; the fill would own 307
and match ~140.  That is ~169 mismatched edges, for **~311/480** projected and
~246/480 measured — against **467/480** today.

This is the third pattern to fail the same way (`DISJOINT_HOLES.md` measured
the 64-cell one at ~350/480).  The rule they all run into:

> **minimise edges at stake, not holes.**  The fill converts edges to matched
> edges at ~46% and the search at ~98%, so every edge handed from the search to
> the fill costs about half a matched edge.  A hole set is good when it is
> *compact*: 8 clustered holes cost 16 edges, 84 scattered ones cost 307.

The instincts behind the idea — disjoint holes make the Hungarian step exactly
optimal, and a big connected region is easy to fill constructively — are both
correct.  They are just worth far less than the edges they cost.

## The same pattern with the DFS traversing in a different order

If the damage is inherited from where the traversal ends, maybe a different
traversal leaves different pieces behind.  `~/EternityII_Solver/random_order.py`
installs a **random valid** order: `Prepare()` requires each cell to be placed
after the cell to its left `(r, c-1)` and the cell below it `(r+1, c)`, and any
linear extension of that partial order satisfies it, so the order can be
randomised without breaking the solver.  (It still has to end at the top-right,
which is the unique maximum of the partial order — so this changes the shape of
the frontier, not the dead corner.)

DFS at budget 13 for 120 s, then the 84 `b` cells punched back out and refilled
by optimal assignment:

| traversal | after DFS + fill | after 84-hole Hungarian | damage cluster |
|---|---|---|---|
| shipped winding | **461** | **461** | 15 cells, rows 0–5 cols 12–15 |
| random order, seed 1 | 400 | 415 | 57 cells, rows 0–7 cols 5–15 |
| random order, seed 2 | 319 | 354 | 106 cells, rows 0–13 cols 2–15 |

**The shipped winding is not arbitrary, it is tuned.**  It keeps the search
frontier compact; a random linear extension makes it ragged, the DFS gets far
less far, and the damage spreads from 15 cells to 106.  Every attempt to move
the traversal has now failed the same way: rotating it crashes (the piece tables
are precomputed per rotation), reordering it starves the search, and the dead
corner is forced by the partial order in the first place.

One good side-effect worth noting: the 84-hole Hungarian step **gained** +15 and
+35 on the two randomised boards, while doing nothing for the shipped one.  That
is the same rule as everywhere else in this session — the fill is worth a lot on
a board that is not converged and exactly zero on one that is.

## Reproducing

```bash
python3 tools/pat84.py                                   # geometry + projection
python3 tools/punch84.py results/best_467.txt /tmp/a.txt punch
./build/eternity-blossom complete --board /tmp/a.txt --pieces data/e2pieces.txt
python3 tools/punch84.py results/best_467.txt /tmp/b.txt swap 1
./build/eternity-blossom complete --board /tmp/b.txt --pieces data/e2pieces.txt
```
