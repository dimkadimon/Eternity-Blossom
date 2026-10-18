# The Blackwood hybrid: 441 → 466

The previous best board this project could produce was **441/480**. Feeding
Joshua Blackwood's constructive DFS solver
(`jblackwood345/EternityII_Solver`) through the Eternity-Blossom
local-optimisation stack reaches **466/480**, verified two ways.

The two halves need each other, and that is the whole point:

- Blackwood's solver is a randomised-restart DFS that fills the board in a
  fixed order, only ever placing a piece whose **left and bottom** edges match
  its neighbours, with a small budget of allowed mismatches ("breaks", ten of
  them, all at search indices ≥ 201). It reaches index ~247 of 256 and stops.
  **As written its boards score 0** — the empty cells make them invalid, so
  every tool here refused them.
- Our own search plateaus at 441 no matter how long it runs (see
  `RECORD_HUNT.md`: 12 runs at 300 s × 2 threads gave mean 438.0, max 441;
  starting *from* the 441 board it never found a 442).

Join them and each half fixes the other's failure.

## Pipeline

```
Blackwood DFS (randomised restart, ~40 s)
   -> partial board, index ~247/256, 9 empty cells, score 0
      -> bw2e2.py            convert numbering, rotation, orientation
         -> complete         fill empties by exact bipartite assignment
            -> cyccover / blast / tjoin   to a fixed point
               -> solve --board           SA + ILS + post-kick repair
```

`scratch/hybrid.sh` runs the whole thing; `scratch/polish_bw.sh` is the
exact-operator fixed point; `scratch/bw2e2.py` is the converter.

## Getting the conversion right

Three things had to be lined up, and each was derived from the piece data
rather than guessed. It took four attempts.

**Rotation.** Eternity-Blossom presents, on side *s*, the colour
`c[(s - rot) & 3]`, so its rotation count is the number of clockwise quarter
turns. Blackwood's `Rotations == 1` has `TopSide = piece.LeftSide`, i.e. new
top = old left, also one clockwise quarter turn. Comparing the two piece files
pins it exactly: Blackwood's piece 1 unrotated is `(T,R,B,L) = (1,17,0,0)` and
Eternity-Blossom's piece 0 at rotation 2 is `(N,E,S,W) = (1,17,0,0)`. So

```
our_rotation = (blackwood_rotation + 2) & 3
```

and, importantly, **the side labels are not flipped** — his `T,R,B,L` are our
`N,E,S,W`. (An earlier attempt assumed a 180° side flip and was wrong.)

**Numbering.** Blackwood numbers pieces 1..256, Eternity-Blossom 0..255, so
our index is his minus one. Verified on pieces 1, 2 and 139. His 0 (no piece)
becomes −1.

**Orientation.** The side labels are not flipped but the *board* is: the two
solvers number rows from opposite ends. All four orientations were tried and
`complete` settles it, because only the right one puts the grey edges on the
border:

| orientation | matched | border_bad |
|---|---|---|
| identity | 244 | 29 |
| **vertical flip** | **458** | **0** |
| horizontal flip | 35 | 58 |
| 180° | 248 | 29 |

`border_bad 0` is a strong check: it means all 64 border slots came out grey.

## Results

Eight rounds, 40 s DFS + 150 s SA each, 2 cores:

| round | after exact completion | after SA |
|---|---|---|
| 1 | 458 | 463 |
| 2 | 465 | 465 |
| 3 | 459 | 465 |
| 4 | 462 | 464 |
| 5 | 460 | 462 |
| 6 | 458 | 462 |
| 7 | 460 | 463 |
| 8 | 461 | **466** |

Mean after SA **463.8**, best **466**.

Two things stand out. First, the exact completion alone already gives
458–465 — the DFS does nearly all the work and the last nine cells cost almost
nothing. Second, SA adds only about +2 on average, and on the best round it
added +5; the boards are already deep local optima, consistent with everything
in `RECORD_HUNT.md`.

## Verification

`results/best_466.txt` was checked twice, by independent implementations:

```
$ ./build/eternity-blossom verify --board results/best_466.txt
matched 466/480  mismatched 14  border_bad 0  every-piece-once yes

$ python3 ../verify2/tools/check_board.py --pieces data/e2pieces.txt \
      --board results/best_466.txt
matched    : 466/480
mismatched : 14
border bad : 0
piece use  : ok (every piece exactly once)
```

The Python checker shares no code with the C++ solver, so a bug in the
incremental scoring cannot hide behind it.

## Where this leaves the record

466 is one edge short of the lowest published E2 result (467) and four short of
the record (470). Extrapolating the measured distribution (mean 463.8, sd ~1.5)
gives best-of-100 ≈ 467 — so more rounds of the same pipeline are unlikely to
reach 470 on their own.

The obvious next lever is DFS *depth*. Each round here used a 40 s DFS that
reached index ~247; a longer DFS reaches 250+ and leaves fewer cells for
`complete` to guess at, which is where the remaining edges are being lost.

**That was tested and it did not help.** A second campaign with a 120 s DFS and
240 s SA per round — about 2.4× the compute:

| round | completed | after SA |
|---|---|---|
| 1 | 462 | 464 |
| 2 | 459 | 463 |
| 3 | 461 | 463 |
| 4 | 462 | 464 |
| 5 | 462 | 464 |
| 6 | 461 | 464 |
| 7 | 461 | **466** |
| 8 | 460 | 465 |

Mean 464.1, best **466** — the same ceiling as the cheaper campaign, and a
tighter spread (min 463 vs 462). Sixteen rounds across both budgets span only
462–466, so 466 looks like the plateau of this pipeline rather than a lucky
draw, and spending more on the DFS is not where the remaining edges are.

## What did NOT work

Optimising Blackwood's C# directly was a dead end, and it is worth recording
why, because the negative result is sharp:

- **Packing candidates into a uint32** (PieceNumber, TopSide, RightSide,
  Rotations, Break_Count, Heuristic_Side_Count fit in 26 bits) made it **~25%
  slower**. Extracting a field became shift → mask → compare → branch, a serial
  dependency chain, whereas loading three fields of an 8-byte struct lets the
  loads issue in parallel from L1. On an out-of-order CPU the parallel loads win.
- **Flattening `master_piece_lookup[cell][key]` into one CSR table** also lost
  ~30%. The original shares only ~10 distinct 529-entry tables across all 256
  cells — about 36 KB, comfortably L2-resident. Materialising 256×529 separate
  lists produced a 541 KB offset table and destroyed the locality it was meant
  to improve.
- Instrumented, the original does only **~2.3 candidate-scan iterations per
  node**, so the inner scan is not the hot spot at all; the fixed per-node path
  is. Both of the above optimised the part that does not matter.

Baseline for reference: ~110–125 M nodes/sec at 2 threads, best index 247–250.
The instrumentation and benchmark harness added to Blackwood's `Program.cs`
(`--threads --solves --seconds --mode --export`) is kept, since it is what made
any of this measurable.
