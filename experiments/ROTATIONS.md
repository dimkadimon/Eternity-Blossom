# Rotating boards: giving the crossover parents that are strong in different places

## The idea

Crossover has failed all session in the same specific way, and `childBetter`
measures it exactly: across every configuration tried, **not one child was ever
born better than both its parents**.  The obvious explanation was that the
parents were too alike — every board in the population is wounded in the same
place, rows 0–5 cols 9–15, because that is where Blackwood's DFS runs out of
board.  Two parents damaged identically have nothing complementary to combine.

The board is a square, so rotating a whole board by a quarter turn is a symmetry
of the puzzle: pieces go to pieces, the border goes to the border, rotations
advance by one, and the score is preserved.  What changes is **where the damage
sits**.  `tools/rotate_board.py`:

```
cell (r, c) -> (c, 15 - r)          rotation -> (rotation + 1) % 4
```

All three 467 boards rotated to 1, 2 and 3 quarter turns verify clean:

```
matched 467/480  mismatched 13  border_bad 0  every-piece-once yes
```

and the wound genuinely moves:

| orientation | biggest unhappy cluster |
|---|---|
| as found | rows 0–5, cols 9–14 (top-right) |
| 1 turn | rows 9–14, cols 10–15 (bottom-right) |
| 2 turns | rows 10–15, cols 1–6 (bottom-left) |
| 3 turns | rows 1–6, cols 0–5 (top-left) |

Twelve seeds — three boards × four orientations — all valid 467s, wounded in
four different corners.  A parent wounded bottom-left is strong exactly where a
parent wounded top-right is weak.  That is the complementary material the
crossover has never had.

## The result

Every parent is at cost 26 (467), so a child better than both parents **is** a
468.  Four configurations, 900 s each, population 12:

| transplant size | candidates per child | child cost mean | best child | children better than both parents |
|---|---|---|---|---|
| 1–4 (4–16 cells) | 256 | 32 | 26 | **0 / 75** |
| 6–10 (36–100 cells) | 64 | 95 | 26 | **0 / 75** |
| 6–10 | 512 | 78 | 26 | **0 / 75** |
| 2–6 | 512 | 42 | 26 | **0 / 75** |

Improvements: **0**, in all four.  Not one child improved on its parents at all,
let alone reached 468.  With 512 candidates per child that is roughly 90,000
evaluated transplants across the four runs, and the best of them merely ties.

## Why, and why it matters

Complementarity was not the missing ingredient.  The transplant takes a
rectangle from the donor and drops it into the same position in the recipient,
and the damage it does is at the **boundary** of that rectangle: the donor's
pieces were chosen to match the *donor's* surroundings, and the recipient's
surroundings are different.  Bringing in a region that is perfect in its
original context does not help, because the context is exactly what does not
carry over.

So the crossover fails for a structural reason that rotating cannot fix — and
this experiment is the one that pins it down, because it removes every other
candidate explanation.  The parents are as complementary as it is possible to
make them, the operator has been given 512 tries per child to find a good
transplant, and it still cannot produce a single improvement.

## What survives

`tools/rotate_board.py` is a free 4× multiplier on any board and is worth
keeping: rotations are guaranteed-valid distinct boards, useful for seeding and
for checking whether a result is an artefact of one orientation.  All twelve
rotated seeds are in `scratch/part/rotseeds`.

```
python3 tools/rotate_board.py BOARD OUT [1|2|3]
```
