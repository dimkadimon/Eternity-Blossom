# The 64-disjoint-hole idea, measured

**The idea.** Fill the 192 `b` cells of this pattern with Blackwood's DFS and
then place the remaining 64 pieces into the `d` cells with Hungarian matching:

```
row  0:  b b b b b b b b b b b b b b b b     even rows: all b
row  1:  d b d b d b d b d b d b d b d b     r = 1 mod 4: d on even columns
row  2:  b b b b b b b b b b b b b b b b
row  3:  b d b d b d b d b d b d b d b d     r = 3 mod 4: d on odd columns
```

64 `d` cells, no two adjacent (two apart along a row, and the rows carrying them
are two apart), so every one of their four edges faces a filled `b` cell or the
border — and the 192 `b` cells are one connected component.  The point of the
disjointness is that it makes the assignment *exactly* optimal, which is the
part that is right.  What follows is the measurement of the part that is not.

## The premise is correct

Punch those 64 cells out of the 466 board and let `complete` refill them:

```
edges at stake    240  (hole-hole 0, hole-filled 240)   frozen: 232/240 already matched
bipartite fill    234/240  ->  board 466/480
branch and bound  234/240  ->  board 466/480  (1 nodes, proved optimal)
```

With no hole-hole edges the bipartite assignment and the branch and bound agree
exactly — the matrix prices every at-stake edge against a real, already-filled
neighbour, so the Hungarian result *is* the optimum.  Same thing showed up in
`HOLEFILL.md` on scattered punches (23/23, 29/32, 32/32).  The premise holds.

## But ~45% of the edges handed to the fill come back matched

Park 64 arbitrary pieces in the `d` cells (swap them with 64 random `b` cells,
which also scrambles the boundary colours the `d` cells see — exactly what a `b`
region built with no regard for the `d` cells looks like), punch, refill:

| trial | fill | |
|---|---|---|
| 1 | 109/240 | |
| 5 | 110/240 | |
| 7 | 113/240 | |
| 9 | 115/240 | |

~112 of 240, i.e. **47%**.  The other trials came back at −1900 to −3900 because
16 of the `d` cells sit on the border and the arbitrary pieces could not show
grey there; those are the trials where the fill did not even produce a legal
board, which is itself part of the answer.

That 47% is not a property of the pattern — it is the fill's rate, and it is the
same rate everywhere:

* the 16 real DFS partial boards fill 5–9 of 12–18 at-stake edges → **44%**;
* 8 scattered holes punched out of a finished board → perfect (those pieces are
  the ones that belong there, so it is a different question).

## Why that sinks it: the two stages have very different hit rates

| who decides an edge | matched |
|---|---|
| the DFS (filled–filled edges) | 454/464 = **97.8%**  (232/240 on the b-b edges of the 466 board) |
| the fill (edges touching a hole) | **~45%** |

So every edge moved from the fill to the DFS is worth about +0.53 matched edges.

* **Now:** 8 clustered holes, 16 edges at stake, ~7 matched → 9 lost.
* **Proposed:** 64 holes, 240 edges at stake, ~112 matched → **~128 lost.**

A perfect `b` region (240/240) plus a 47% fill lands near **350/480**, against
**466** today.  The idea is not "a bit worse" — it is 110 edges worse, because it
hands half the board to the weaker of the two stages.

It also fails as a constructive matter: a randomised most-constrained-first
greedy could only place 98 of the 192 `b` cells before running out of pieces
that fit.  A real backtracking DFS would do better, but a 0-break `b` region is
not the free lunch the loose cell count suggests.

## The principle, stated positively

**Minimise the number of edges at stake, not the number of holes.**  The fill
matches ~45% of whatever it is given, so what matters is the *isoperimetry* of
the hole set:

| 8 holes | edges at stake |
|---|---|
| clustered in a corner (what the DFS produces today) | **16** |
| disjoint, hugging the border (cheapest disjoint layout) | ~22 |
| disjoint, interior | 32 |
| the 64-hole `d` pattern | 240 |

This is the same reason `HOLEFILL.md` concluded that making the holes
non-adjacent to make the *Hungarian* exact is the wrong trade: exactness of the
assignment buys ~2 edges, and the extra edges at stake cost far more.  Solve the
fill exactly *and* keep the hole set tight — which is what `e2/holefill` does.

## Reproducing

```bash
python3 tools/punch64.py results/best_466.txt      # punch the 64 d cells
./build/eternity-blossom complete --board results/best_466.p64.txt --nodes 1
python3 tools/swaptest.py results/best_466.txt /tmp/s.txt 1   # arbitrary pieces
./build/eternity-blossom complete --board /tmp/s.txt --nodes 1
python3 tools/fillb.py /tmp/b.txt 40                          # fill the b region
```
