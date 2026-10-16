# Piece data

## e2pieces.txt -- the authentic Eternity II piece set

256 rows, four integers per row, the four edge colours **clockwise from North**
(`N E S W`).  Colour `0` is grey, the border colour.

Structural checks that `eternity-blossom verify` performs on load -- all of them
hold for this file:

| invariant | value |
|---|---|
| pieces | 256 = 16x16 cells |
| distinct colours | 23 (grey + 22) |
| grey edges | 64 -- exactly the number of border slots |
| pieces with 2 grey edges | 4 (the corners) |
| pieces with 1 grey edge  | 56 (the rest of the perimeter) |
| every non-grey colour count | even (necessary: internal adjacencies pair stubs) |

Colour multiplicities: five colours appear 24 times, five appear 48 times, twelve
appear 50 times, grey appears 64 times (24+48+50+52 = 1024 = 256x4).

### Caveats

* The file is in **canonical sorted form** (rows sorted lexicographically, each
  piece rotated so that its grey edges come first).  The piece *multiset* is the
  real one, but the **numbering does not match the official printed numbering**,
  so the official "starter piece 139" clue cannot be applied to it.  The search
  does not use that clue.
* The encoding convention was confirmed empirically, not from documentation:
  with the consecutive-sides reading, all four two-grey pieces have their grey
  edges on adjacent sides (positions 0 and 1) and all 56 one-grey pieces have
  theirs on side 0.  The alternative reading (`N S W E`) would put the corner
  pieces' grey edges on opposite sides, which cannot happen at a corner.

## Generated instances

`instances/gen{N}x{N}.txt` come from `tools/gen_instance.py`, which lays down a
random solved board and reads the pieces off it, so each has a **known perfect
solution** in `instances/gen{N}x{N}.sol`.  They are the correctness benchmark:
the solver reaches 100% on 4x4, 5x5 and 6x6, and is measured on 8x8 and 10x10.
