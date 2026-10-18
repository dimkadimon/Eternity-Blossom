# Filling the holes exactly

Blackwood's constructive DFS hands over a partial board with 6–9 empty cells
and the same number of leftover pieces — the ones the search could not place
anywhere.  `complete` used to fill them by minimum-weight bipartite assignment
(Hungarian).  This is what replacing that with an exact branch and bound buys,
what it does *not* buy, and what the geometry of the holes has to do with it.

## The gap the assignment leaves

The cost matrix is

    cost[piece][hole] = min over the 4 rotations of
                        (mismatched edges against the hole's CURRENT neighbours)

That is the true cost only when every neighbour of every hole is already
filled.  When two holes touch, each prices the shared edge against whatever
placeholder the other still holds, and the two rotations are chosen
independently — so the assignment cannot, even in principle, decide to make
that edge match.

The boards make this worse than it sounds.  `Util.Get_Board_Order()` ends with a
3×3 block in a corner (indices 244–255), so the cells left empty are always a
tight cluster there.  That is *good* — it minimises the number of edges at
stake — but it means most of the edges at stake are hole-hole ones:

| board | holes | at stake | hole-hole | hole-filled |
|---|---|---|---|---|
| cheap 401 | 8 | 16 | 10 | 6 |
| cheap 405 | 9 | 18 | 12 | 6 |
| cheap 402 | 6 | 12 | 7 | 5 |
| exp 407 | 7 | 14 | 8 | 6 |

Eight cells clustered in a corner put only ~2 edges per hole at stake; eight
non-adjacent interior cells would put 4 each — 32 in total.

## What the exact fill gives

`e2/holefill` is a branch and bound over the holes: choose piece, cell and
rotation to maximise matched edges over every edge touching a hole (hole-hole
included), border kept grey, with the admissible bound

    gain + Σ over unplaced holes (best fixed-neighbour gain still available)
        + (hole-hole edges not yet decided)

The bipartite solution seeds the search, so the result is never worse than it.
Cost is 10²–10⁵ nodes for 6–9 holes; 16 holes needs ~2×10⁵ when the cells are
near each other.

On the 16 recovered DFS partial boards (+1…+4 edges, mean **+2.25**):

| board | bipartite | exact | | board | bipartite | exact |
|---|---|---|---|---|---|---|
| cheap 401 | 458 | **461** | | exp 401 | 461 | **463** |
| cheap 402 | 463 | **464** | | exp 402 | 457 | **460** |
| cheap 403 | 457 | **460** | | exp 403 | 459 | **461** |
| cheap 404 | 459 | **461** | | exp 404 | 459 | **461** |
| cheap 405 | 458 | **459** | | exp 405 | 461 | **462** |
| cheap 406 | 457 | **461** | | exp 406 | 461 | **462** |
| cheap 407 | 459 | **461** | | exp 407 | 461 | **464** |
| cheap 408 | 460 | **462** | | exp 408 | 460 | **461** |

Two independent implementations agree on all 16 optima: the C++ one in
`src/holefill.cpp` and a deliberately separate pure-Python branch and bound
(`scratch/part/holes.py`).

## The tempting alternative, and why it loses

If the holes did not touch, the bipartite assignment would already be exact —
verified: punch eight pairwise non-adjacent cells out of a finished board and
`complete` reports the bipartite fill and the branch and bound agreeing exactly
(23/23, 29/32, 32/32).  So the premise is sound; it is the conclusion that
fails, because non-adjacent holes cost more edges than the assignment's
suboptimality does:

* clustered in a corner: 16 edges at stake for 8 holes, and the exact fill
  matches 7 of them (44%);
* non-adjacent in the interior: 32 edges at stake — breaking even would need
  a **72%** fill rate;
* non-adjacent along the border (the cheapest non-touching arrangement, using
  corners): ~22–23 edges at stake, still needing ~65%.

The leftover pieces are junk — they are what a 248-cell search rejected — and
no amount of clever pricing gets 72% out of them.  Keeping the tight cluster
and solving the fill properly is strictly better than spreading the holes out
to make a weaker solver optimal.

## What it does NOT buy: the final score

Replaying stages 3–5 of the pipeline on the *same* partial boards with the
*same* SA seeds, changing only the fill:

| round | completed (bipartite) | after SA | completed (exact) | after SA |
|---|---|---|---|---|
| 1 | 458 | 463 | 461 | 462 |
| 2 | 465 | 465 | 465 | 465 |
| 3 | 459 | 465 | 460 | 464 |
| 4 | 462 | 464 | 461 | 464 |
| 5 | 460 | 462 | 461 | 462 |
| 6 | 458 | 462 | 461 | 462 |
| 7 | 460 | 463 | 461 | 463 |
| 8 | 461 | **466** | 462 | **466** |

Mean completed 460.4 → 461.5.  Mean after SA **463.75 → 463.5** — unchanged.
The operators in `polish_bw.sh` recover part of the gap on their own (they
lift the bipartite boards by +0…+3), and the SA erases the rest.

The reason is that these boards are *deep* local optima.  Three probes:

1. **Punch the damaged cells and re-solve exactly.** Taking the 8 cells that
   cover 12 of the 19 broken edges, or 16 cells covering all 19, and
   re-optimising them exactly returns the same 461.
2. **Punch random cells.** Always puts the pieces straight back.
3. **Widen the hole set.** Freeing the 8 holes plus the 4 nearest filled cells
   (12 cells, 24 edges at stake, 9.7 M nodes, proved optimal) also gives 461.

So the completion is no longer the bottleneck — the spread of where the SA
lands is.  Best-of-N is the remaining lever: 16 boards × several SA seeds,
which is what `scratch/part/chase.sh` runs.

## Reproducing

```bash
bash scripts/build.sh
./build/eternity-blossom complete --board <partial.txt> --out full.txt
#   edges at stake    16  (hole-hole 10, hole-filled 6)   frozen: 454/464 already matched
#   bipartite fill    4/16  ->  board 458/480
#   branch and bound  7/16  ->  board 461/480  (20125 nodes, proved optimal)
```

`--nodes N` caps the search (default 2×10⁷); if it runs out the best solution
found so far is still returned and reported as `NODE LIMIT`.
