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

## Reproducing

```bash
python3 tools/pat84.py                                   # geometry + projection
python3 tools/punch84.py results/best_467.txt /tmp/a.txt punch
./build/eternity-blossom complete --board /tmp/a.txt --pieces data/e2pieces.txt
python3 tools/punch84.py results/best_467.txt /tmp/b.txt swap 1
./build/eternity-blossom complete --board /tmp/b.txt --pieces data/e2pieces.txt
```
