#!/usr/bin/env python3
"""Punch out the damaged part of a board so `complete` can re-optimise it exactly.

Every 467 board found so far has the SAME shape of damage: 13 mismatches over
21 unhappy cells, and almost all of it in one connected cluster of 16 cells in
rows 0-5, cols 9-14 -- the corner where Blackwood's constructive DFS runs out of
board and leaves its holes.  The damage is inherited, not scattered.

So instead of annealing the whole board, empty the damaged cells and let the
exact fill put the pieces back.  Three neighbourhoods, because the smallest one
may be provably optimal already while a larger one lets pieces move in and out
of the region:

  unhappy  -- just the cells touching a mismatched edge
  ring     -- those cells plus everything adjacent to them
  box      -- the bounding box of the big cluster, including happy cells

Usage:
    python3 tools/damagerepair.py BOARD OUT [unhappy|ring|box]
"""
import sys

N = 16


def load_pieces(path):
    p = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        v = [int(x) for x in line.replace(",", " ").split()]
        if len(v) >= 4:
            p.append(v[:4])
    return p


def load_board(path):
    rows = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        v = [int(x) for x in line.replace(",", " ").split()]
        if len(v) >= 2:
            rows.append((v[0], v[1]))
    return rows


def rot(pc, r):
    return [pc[(4 - r) % 4], pc[(5 - r) % 4], pc[(6 - r) % 4], pc[(7 - r) % 4]]


def unhappy_cells(board):
    cells = set()
    for r in range(N):
        for c in range(N):
            p = board[r * N + c]
            if c + 1 < N and p[1] != board[r * N + c + 1][3]:
                cells.add((r, c))
                cells.add((r, c + 1))
            if r + 1 < N and p[2] != board[(r + 1) * N + c][0]:
                cells.add((r, c))
                cells.add((r + 1, c))
    return cells


def main():
    src, out = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else "unhappy"

    pieces = load_pieces("data/e2pieces.txt")
    rows = load_board(src)
    board = [rot(pieces[pid], r % 4) for pid, r in rows]

    cells = unhappy_cells(board)
    if mode == "ring":
        extra = set()
        for (r, c) in cells:
            for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                nb = (r + dr, c + dc)
                if 0 <= nb[0] < N and 0 <= nb[1] < N:
                    extra.add(nb)
        cells |= extra
    elif mode == "box":
        rs = [r for r, _ in cells]
        cs = [c for _, c in cells]
        cells = {(r, c) for r in range(min(rs), max(rs) + 1)
                 for c in range(min(cs), max(cs) + 1)}

    cells = sorted(cells)
    outrows = list(rows)
    for (r, c) in cells:
        outrows[r * N + c] = (-1, 0)

    with open(out, "w") as f:
        f.write("# Eternity-Blossom board\n")
        f.write(f"# {len(cells)} cells emptied ({mode}) for exact re-optimisation\n")
        for pc, rt in outrows:
            f.write(f"{pc} {rt}\n")
    print(f"{out}: emptied {len(cells)} cells ({mode})")


if __name__ == "__main__":
    main()
