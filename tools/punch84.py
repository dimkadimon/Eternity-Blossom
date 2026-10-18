#!/usr/bin/env python3
"""Punch the 84 'b' cells of the 16x16 a/b labelling out of a board.

The labelling is a perfect independent set: not one of the 84 b cells touches
another, so all 307 internal edges touching them face a filled a cell, and 29
of their sides face the board border.  Compare with the 8 clustered holes the
real pipeline uses, which put only 16 edges at stake.

Two modes:

  punch   -- empty the b cells and leave the pieces that belong there unused.
             Filling them back with `complete` shows how much of the b region
             is already determined by the a region: the ceiling for the
             two-stage plan, if the search happened to leave exactly the right
             84 pieces over.

  swap    -- first park 84 *arbitrary* pieces in the b cells (swapped with 84
             random a cells), then punch.  That is what you actually get from a
             search that fills the a region without regard for the b cells, and
             it is the number that decides whether the plan works.

Usage:
    python3 tools/punch84.py BOARD OUT [swap SEED]
"""
import random
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from pat84 import PATTERN, parse  # noqa: E402

N = 16


def bcells():
    rows = parse(PATTERN)
    return [(r, c) for r in range(N) for c in range(N) if rows[r][c] == "b"]


def read_board(path):
    cells = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        v = [int(x) for x in line.replace(",", " ").split()]
        if len(v) >= 2:
            cells.append((v[0], v[1]))
    assert len(cells) == 256, f"{len(cells)} cells in {path}"
    return cells


def write_board(path, cells):
    with open(path, "w") as f:
        f.write("# Eternity-Blossom board\n")
        f.write(f"# board 16x16 pieces {len(cells)}  (84 b cells punched out)\n")
        for pc, rt in cells:
            f.write(f"{pc} {rt}\n")


def main():
    src, out = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else "punch"
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1

    cells = read_board(src)
    holes = bcells()
    assert len(holes) == 84
    hset = set(holes)

    if mode == "swap":
        # Park arbitrary pieces in the b cells: swap them with random a cells.
        # This also scrambles the colours the b cells see, which is exactly what
        # an a-region built without regard for them looks like.
        rng = random.Random(seed)
        others = [i for i in range(256) if i not in hset]
        picks = rng.sample(others, len(holes))
        for (hr, hc), src_i in zip(holes, picks):
            h = hr * N + hc
            cells[h], cells[src_i] = cells[src_i], cells[h]

    for (r, c) in holes:
        cells[r * N + c] = (-1, 0)

    kept = sum(1 for pc, _ in cells if pc >= 0)
    write_board(out, cells)
    print(f"{out}: {256 - kept} holes, {kept} filled "
          f"({'swapped' if mode == 'swap' else 'right pieces left over'})")


if __name__ == "__main__":
    main()
