# Changing the local search: what the damage actually looks like

Every 467 board found so far was mapped before trying to improve it, and the
map turned out to be the most useful thing measured all session.

## All the 467s are damaged in exactly the same place

`tools/mismap.py` reports the mismatched edges, the cells that touch them, and
how those cells cluster.  For all three independent 467 boards:

```
13 mismatches, 21 unhappy cells
unhappy-cell components: [16, 3, 2]
  cluster of 16 cells, rows 0-5, cols 9-14
  cluster of 3 cells,  rows 3-4, cols 5-6
  cluster of 2 cells,  rows 4-5, cols 9-9
colours wanted: (4,3) (2,2) (19,2) (22,2) (6,1) (12,1)
```

Identical, to the cell.  Three boards found on different seeds with different
operators carry the same defect, which means it is inherited rather than
random: **rows 0–5, cols 9–14 is the corner where Blackwood's constructive DFS
runs out of board.**  `Util.Get_Board_Order()` winds through the top-right
corner last, so the cells it cannot fill are always there, and every downstream
stage inherits the wound.

So the damage is concentrated, not scattered — which is good news for a local
move, if any local move can touch it.

## Three attempts to fix that cluster, all negative

**T-join** (`tjoin` — connect the unhappy cells with a minimum-weight T-join and
rotate along each chain; Blossom, non-bipartite).  This is the move designed
precisely for a board with a handful of bad edges, and it exists as a standalone
command but has never been part of the search:

```
passes 20, improving 0, chains 200, cells moved 0
cost 26 -> 26  (gain 0),  score 467 -> 467/480
```

200 chains, not one of them improving.

**Punch the damage out and refill it exactly** (`tools/damagerepair.py`, three
neighbourhoods).  The fill cannot even reconstruct what was there, because it
prices each cell independently and two emptied cells are often adjacent, so the
true cost is not additive:

| emptied | edges at stake | fill | board |
|---|---|---|---|
| 21 unhappy cells | 62 | 45 | 463 |
| + 1-ring (52 cells) | 121 | 51 | 410 |
| bounding box (60 cells) | 126 | 26 | 380 |

The branch and bound hit 2,000,000 nodes on all three without closing the gap.

**Target the region LNS at the damage.**  The shipped LNS grows its region from
a uniformly random cell, so most passes land on parts of the board that are
already perfect.  `--region-targeted` (new) grows it from a cell in the
search's own `conflicts` list instead.  With region size 16 — the whole damaged
cluster:

```
region runs   : 170 (improved 0)  nodes 24739437  bound calls 14314989  bound prunes 13298571
```

**170 passes, 24.7 million nodes, zero improvements.**  That is a much stronger
statement than "the move did not help": the damaged cluster is *exactly
optimal*.  There is no rearrangement of the pieces inside it, with its boundary
held fixed, that reduces a single mismatch.  Whatever reaches 468 has to move
the boundary, not the contents.

## Trying to stop inheriting the defect

If the damage comes from where the DFS runs out of board, move the dead corner.
`tools/rotate_order.py` rotates `Get_Board_Order()` by quarter turns.  It
crashes:

```
IndexOutOfRangeException at Opt.cs:169 (Prepare)
```

`Prepare()` hard-codes the two predecessor neighbours of each cell as
left (`col-1`) and below (`row-1`), and `SolvePuzzle` builds its candidate key
as `leftSide * 23 + belowCell.TopSide`, indexing tables of pieces precomputed by
rotation.  A rotated traversal would need the whole side-indexing convention
rotated with it — not a small change, and not worth it on its own.

## New seed boards

`~/EternityII_Solver/genseeds.sh` generates fresh boards (break budget 13,
120 s each, converted and filled).  Six came out at 464, 463, 461, 464, 463,
464.  `scratch/part/newseeds3` mixes the best of those with the best older
boards and **deliberately excludes every 467**, so that a 467 from the next run
is a find rather than an elite survivor:

```
466 466 464 464 464 463 463 463 462 461 461 459
```

Two runs from that set — one with the targeted region LNS, one without — both
**reached 467**, which is a genuine result this time because the best seed was
466.  Targeted and untargeted scored the same (467, 19 vs 22 improvements),
consistent with the cluster being exactly optimal.  Two more distinct 467
boards: `results/best_467_new1.txt`, `results/best_467_new2.txt`.

## The damage is in the same place in every lineage

`tools/mismap.py` on boards built three different ways:

| board | how it was built | biggest unhappy cluster |
|---|---|---|
| `conv_b16` (464) | DFS, budget 16, completed | rows 0–5, cols 9–15 |
| `best_466` (466) | DFS, budget 10, 8 holes | rows 0–5, cols 10–15 |
| `best_466_b13` (466) | DFS, budget 13, 2 holes | rows 0–4, cols 9–14 |

Break budget 10, 13 or 16 — hole count 8, 2 or 0 — it makes no difference.  The
wound is always in the same corner, because that is where the traversal ends.
The inheritance is universal, not an artefact of one setting.

## A sealed neighbourhood: `--focus r0:r1:c0:c1`

The cluster is exactly optimal, so the move that could matter is one that lets
pieces cross its boundary.  Branch and bound cannot search a region that big, so
anneal it instead: `--focus` confines the *entire* swap neighbourhood — the
conflict list, the aimed moves, the best-of-K proposals, the perturbation kick
and the post-kick repair — to a rectangle.

```
./build/eternity-blossom solve --board BOARD --focus 0:5:8:15 ...
```

Validation, from a 461 board, 150 s, with the global operators off
(`--assign 2 --no-strip --no-coarse`):

| | result |
|---|---|
| unfocused | 464 |
| confined to 48 cells | **463** |

The confined search visibly improves less, so the rectangle really is sealed.
(The first version of this looked like a no-op because `--assign 256` runs a
Hungarian reassignment over the whole board and simply routed around it; the
rectangle only bites once that and the kick are confined too.)

Aimed at the damaged corner of a 467, with the boundary free to move:

| | result |
|---|---|
| rows 0–5, cols 8–15 (48 cells), t0 1.5 | 467 |
| rows 0–6, cols 6–15 (70 cells), t0 3.0 | 467 |

**The boundary-moving move does not work either.**  A 48- or 70-cell rectangle
covering the damage and its surroundings, annealled for 300 s with everything
outside it frozen, cannot find a single edge.

## The board representation has no concept of a hole

Worth recording because it rules out a whole family of moves.  Running `solve`
on a board with empty cells segfaults, at `Board::rebuild()`:

```cpp
const int p = pc[cell];
const uint8_t* src = &protCol[p * 16 + rt[cell] * 4];   // p = -1 -> out of bounds
loc[p] = cell;                                          // loc[-1] -> smashed
```

`Board` assumes every cell holds a piece; only `holefill` (used by `complete`)
knows about empties.  So "punch out the damaged region and let the SA refill it"
— which would be the natural way to beat the fill's ~46% — needs the board
representation taught about holes first, and every operator along with it.  Not
attempted.

## Sampling with the new boards

`scratch/part/campaign468b.log`, seeds 400–401 from `newseeds3` (best seed 466,
so nothing is inherited):

```
400  t0 0.60 breed 12   466   466
401  t0 0.70 breed 12   467   467     <- found, not preserved
```

## What is left

The local search has now been shown optimal against: single swaps, 3-cycles,
rotation strips, coarse block moves, Blossom swap packets, Hungarian
reassignment, exact region branch and bound at sizes 6/8/12/16 (targeted and
untargeted), and T-join chains.  The one direction not yet tried is *growing*
the region past the cluster so the boundary itself can move — and the
21/52/60-cell experiments above suggest the exact fill is not up to it.  A
different way to say it: the 467s are not slightly-wrong boards that a better
move could polish.  They are the same board, wounded in the same place, and the
wound is inherited from the constructive solver.
