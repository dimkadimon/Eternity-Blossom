#!/usr/bin/env python3
"""Independently verify a board produced by the C++ solver.

This is a deliberately separate implementation (pure Python, no shared code) so
that a bug in the C++ incremental scoring cannot hide: it re-derives the score
from the piece file and the board file, checks that every piece is used exactly
once, and reports the matched-edge count the way the Eternity II community counts
it (out of the 2*W*(H-1) + 2*H*(W-1) ... i.e. the number of internal adjacencies).

usage:
    python3 tools/check_board.py --pieces data/e2pieces.txt --board results/best.txt
"""
import argparse
import sys


def load_pieces(path):
    pieces = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                vals = [int(x) for x in line.split()]
                assert len(vals) == 4, f"bad piece line: {line}"
                pieces.append(vals)
    return pieces


def load_board(path):
    pc, rt = [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p, r = (int(x) for x in line.split())
            pc.append(p)
            rt.append(r)
    return pc, rt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pieces", default="data/e2pieces.txt")
    ap.add_argument("--board", default="results/best_board.txt")
    ap.add_argument("--w", type=int, default=0, help="board width (default: sqrt of #cells)")
    args = ap.parse_args()

    pieces = load_pieces(args.pieces)
    pc, rt = load_board(args.board)
    n = len(pieces)
    if len(pc) != n:
        print(f"FAIL: board has {len(pc)} cells but there are {n} pieces")
        return 1
    w = args.w or int(round(n ** 0.5))
    h = n // w
    assert w * h == n, f"{n} is not a rectangle of width {w}"

    used = [0] * n
    for p in pc:
        used[p] += 1
    bad_use = sum(1 for u in used if u != 1)
    if bad_use:
        print(f"FAIL: {bad_use} pieces are not used exactly once")
        return 1

    def colour(cell, side):
        return pieces[pc[cell]][(side - rt[cell]) % 4]

    matched = mismatched = 0
    for r in range(h):
        for c in range(w):
            cell = r * w + c
            for side, (dr, dc) in enumerate(((-1, 0), (0, 1), (1, 0), (0, -1))):
                rr, cc = r + dr, c + dc
                if not (0 <= rr < h and 0 <= cc < w):
                    continue
                nb = rr * w + cc
                if nb < cell:
                    continue  # count each adjacency once
                if colour(cell, side) == colour(nb, (side + 2) % 4):
                    matched += 1
                else:
                    mismatched += 1

    border_bad = 0
    for r in range(h):
        for c in range(w):
            cell = r * w + c
            for side, (dr, dc) in enumerate(((-1, 0), (0, 1), (1, 0), (0, -1))):
                rr, cc = r + dr, c + dc
                if 0 <= rr < h and 0 <= cc < w:
                    continue
                if colour(cell, side) != 0:
                    border_bad += 1

    total = matched + mismatched
    print(f"board      : {w}x{h}, {n} pieces")
    print(f"matched    : {matched}/{total}")
    print(f"mismatched : {mismatched}")
    print(f"border bad : {border_bad}")
    print(f"piece use  : ok (every piece exactly once)")
    if mismatched == 0 and border_bad == 0:
        print("*** PERFECT SOLUTION ***")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
