#!/usr/bin/env python3
"""Map the mismatched edges of a board: where they are, what colour they want,
and whether they form clusters a local move could actually grab.

Usage: python3 tools/mismap.py BOARD [BOARD ...]
"""
import sys
from collections import Counter

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


def main():
    pieces = load_pieces("data/e2pieces.txt")
    for path in sys.argv[1:]:
        b = [rot(pieces[pid], r % 4) for pid, r in load_board(path)]
        assert len(b) == 256, len(b)

        mis = []          # (kind, r, c, colourA, colourB)
        for r in range(N):
            for c in range(N):
                p = b[r * N + c]
                if c + 1 < N:
                    q = b[r * N + c + 1]
                    if p[1] != q[3]:
                        mis.append(("H", r, c, p[1], q[3]))
                if r + 1 < N:
                    q = b[(r + 1) * N + c]
                    if p[2] != q[0]:
                        mis.append(("V", r, c, p[2], q[0]))

        # cells touched by a mismatch
        cells = set()
        for kind, r, c, _, _ in mis:
            cells.add((r, c))
            cells.add((r, c + 1) if kind == "H" else (r + 1, c))

        # connected components of the unhappy cells
        seen = set()
        comps = []
        for cell in sorted(cells):
            if cell in seen:
                continue
            stack, comp = [cell], []
            seen.add(cell)
            while stack:
                (r, c) = stack.pop()
                comp.append((r, c))
                for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                    nb = (r + dr, c + dc)
                    if nb in cells and nb not in seen:
                        seen.add(nb)
                        stack.append(nb)
            comps.append(comp)

        print(f"=== {path}: {480 - len(mis)}/480, {len(mis)} mismatches, "
              f"{len(cells)} unhappy cells")
        sizes = sorted((len(c) for c in comps), reverse=True)
        print(f"    unhappy-cell components: {sizes}")
        for comp in sorted(comps, key=len, reverse=True):
            if len(comp) >= 2:
                rs = [r for r, _ in comp]
                cs = [c for _, c in comp]
                print(f"      cluster of {len(comp)} cells, "
                      f"rows {min(rs)}-{max(rs)}, cols {min(cs)}-{max(cs)}")
        print(f"    colours wanted: {Counter([m[3] for m in mis]).most_common(6)}")
        # how many mismatches are isolated (both cells in a size-2 component)
        iso = sum(1 for c in comps if len(c) == 2)
        print(f"    isolated mismatches (component of exactly 2 cells): {iso}")
        print()


if __name__ == "__main__":
    main()
