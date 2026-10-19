#!/usr/bin/env python3
"""Rotate a whole board by a quarter turn.

Every board this session has been wounded in the same place -- rows 0-5,
cols 9-15, the corner where Blackwood's DFS runs out of board -- and that is why
crossover has never once produced a child better than both its parents: the
parents are all damaged identically, so there is nothing complementary for a
recombination to combine.

The board is a square, so rotating the whole thing by 90 degrees is a symmetry
of the puzzle: pieces go to pieces, the border goes to the border, and the score
is preserved.  What changes is WHERE the damage sits.  Rotating the population
puts the wound in all four corners and finally gives the crossover two parents
that are strong in different places.

  cell (r, c)  ->  (c, N-1-r)          [90 degrees clockwise]
  rotation     ->  (rotation + 1) % 4

Usage:
    python3 tools/rotate_board.py BOARD OUT [1|2|3]
"""
import sys

N = 16


def load_board(path):
    rows = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        v = [int(x) for x in line.replace(",", " ").split()]
        if len(v) >= 2:
            rows.append((v[0], v[1]))
    assert len(rows) == 256, f"{len(rows)} cells in {path}"
    return rows


def rotate(rows, k):
    out = [(0, 0)] * 256
    for r in range(N):
        for c in range(N):
            pc, rt = rows[r * N + c]
            rr, rc = r, c
            nrt = rt
            for _ in range(k % 4):
                rr, rc = rc, N - 1 - rr
                nrt = (nrt + 1) % 4
            out[rr * N + rc] = (pc, nrt)
    return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    k = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    rows = rotate(load_board(src), k)
    with open(out, "w") as f:
        f.write("# Eternity-Blossom board\n")
        f.write(f"# board 16x16 pieces 256   (rotated {k} quarter turn(s) from {src})\n")
        for pc, rt in rows:
            f.write(f"{pc} {rt}\n")
    print(f"{out}: rotated {k} quarter turn(s)")


if __name__ == "__main__":
    main()
