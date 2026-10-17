# The strong reassignment move: all 4^n · n! placements per (group, slot) pair

## The move

Dissect the board into groups. Take an **independent set** of groups — no two sharing an
edge — and freeze everything else. For each (group, slot) pair, cost the placement by
brute force over **every** way of putting the group's pieces into the slot's cells:

- any of the **n!** assignments of pieces to cells, and
- each of the **4** rotations, chosen **independently for each piece**

so 4^n · n! placements instead of the 4 rigid quarter-turns.

| n | placements |
|---|---|
| 2 (dominoes) | **32** |
| 3 (trominoes) | **384** |
| 4 (tetrominoes) | **6144** |
| 5 | 122 880 |
| 6 | 2 949 120 |

Still **bipartite** — groups on one side, slots on the other — so Hungarian solves it
exactly. Blossom is not involved.

## Two things this changes, both easy to get wrong

**1. The group's internal edges are no longer free.** Under a rigid rotation a group's
internal edges are merely permuted, so their total cancels and can be ignored. Once
pieces permute and spin independently that is false: swapping the two halves of a domino
changes the pair of colours facing each other across its middle. So the internal edges
must be costed, in the baseline *and* in every candidate:

```
cost(gi -> sj) = boundary cost of slot sj  +  internal cost of the arrangement
```

Miss the second term and the predicted gain silently stops matching the board.

**2. The shape no longer has to match — only the size.** Under rigid rotation a horizontal
domino can only fill a horizontal slot, so the assignment had to be split by shape. Now
the pieces are loose tiles: any n-cell group fills any n-cell slot. So the assignment runs
over all groups **of a size**, which on a domino tiling is one big class of ~45 instead of
two classes of ~22.

## Result: bigger polyominoes are *worse*

Each operator run alone, from an identical random start (~26–35/480), swept to
convergence. Three seeds:

| dissection | n | placements/pair | seed 1 | seed 2 | seed 3 | mean | time |
|---|---|---|---|---|---|---|---|
| dominoes, rigid rotation | 2 | 4 | 291 | 300 | 285 | **292** | 0.5 s |
| **dominoes, full** | 2 | **32** | **347** | **341** | **349** | **346** | 1.9 s |
| trominoes, full | 3 | 384 | 327 | 326 | 328 | **327** | 6.2 s |
| tetrominoes, full | 4 | 6144 | 311 | — | — | **311** | 9.0 s |
| 2×2 squares, full | 4 | 6144 | 267 | 277 | 275 | **273** | 1.4 s |
| all combined, full | 2,3,4 | mixed | 349 | 351 | 340 | **347** | 11 s |

So:

* **The full rearrangement is worth about +54 edges over rigid dominoes** (292 → 346).
  That is the big win, and it is cheap: 32 placements per pair, 1.9 s.
* **Dominoes beat trominoes beat tetrominoes.** This was not what I expected. The cause is
  granularity, not coverage — an independent set of tetrominoes actually covers *more*
  cells (28 × 4 = 114) than one of dominoes (45 × 2 = 90). But pieces now move in blocks
  of n, so a single bad edge cannot be repaired without disturbing n−1 others. Finer
  atoms give the assignment more freedom, and that freedom is worth more than coverage.
  Larger groups also lock up a bigger share of their own edges internally: 0.75 of a
  domino's half-edges are on the boundary, but only 0.5 of a 2×2 block's.
* **Combining sizes buys nothing** (347 vs 346) — the dominoes are doing all the work.

## On a board the searcher has already optimised: still zero

| board | rigid | full |
|---|---|---|
| 424/480 | 0 | 0 |
| 441/480 | 0 | 0 |

Not "small" — exactly zero, over 100 dissections, independent set |S| ≈ 44.

Because the move is exact, this can be pushed much further than in the rigid case. Take
each group on its own and ask whether **any** of its 4^n · n! arrangements beats the one
it currently has. This needs no independence at all, since nothing moves between groups,
so it can be asked of every group in every dissection:

```
--inplace: rearrange each group within its OWN cells
  domino#0      groups 128   improvable in place 0   total gain 0
  tromino#1     groups  84   improvable in place 0   total gain 0
  tetromino#0   groups  62   improvable in place 0   total gain 0
  squares2@0,0  groups  64   improvable in place 0   total gain 0
  TOTAL: 0/668 groups improvable in place, total gain 0
```

**Zero of 668 groups**, across dominoes (32 arrangements each), trominoes (384) and
tetrominoes/2×2 (6144). The 441 board is at a fixed point of all of them simultaneously.
That is a genuinely strong local-optimality certificate, and it costs about 6 seconds.

## What the operator is actually good for

Run alone from scratch it reaches ~347/480 and then **stops dead**:

```
$ eternity-blossom polish --board <the 347 board> --time 30
after    : 347/480 matched, cost 266
gain     : 0 edges  (rounds 2, strips 0, blocks 0, sub-boards 0)
ceiling  : now 355/480
```

Its own output is already a fixed point of the searcher's exact operators too, and
rotation alone could add at most 8 more edges (the 355 ceiling). Meanwhile the full
searcher reaches 441. So this operator is a powerful *polisher* and a poor *global
searcher* — it has no perturbation, so it converges and stops, and it has nothing left to
say to a board that has already converged.

Honest summary: **the strong move is worth +54 edges and should replace the rigid one
everywhere, but it does not move the needle on boards the main searcher has produced,
and on its own it plateaus around 347/480.**

## Reproducing

```
# the strong move, from scratch
./build/eternity-blossom dissect --random --seed 1 --time 60 \
    --dissections 400 --mixed 0 --no-strips --no-squares --full

# weak/strong comparison on one board, one pass per dissection
./build/eternity-blossom dissect --board results/best_441.txt \
    --dissections 100 --mixed 0 --no-strips --no-squares [--full] --stats --stats-n 100

# the in-place local-optimality certificate
./build/eternity-blossom dissect --board results/best_441.txt \
    --dissections 3 --trominoes 2 --tetrominoes 2 --mixed 0 \
    --no-strips --no-squares --full --max-cells 4 --stats --stats-n 0 \
    --inplace --inplace-n 7
```

`--trominoes N` / `--tetrominoes N` build polyomino tilings (shapes generated by
`AllOrientations`, so all orientations of both trominoes and all five free tetrominoes
are available); `--max-cells N` caps the brute force; `--mis-trials` / `--mis-improve`
grow the independent set.

Self-test `[7/7]` now checks both modes, and verifies `SlotCostFull` against a naive
enumeration that rebuilds the whole board for every placement and reads the true objective
off it.
