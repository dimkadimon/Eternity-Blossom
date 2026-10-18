#!/usr/bin/env python3
"""Punch the 64 'd' cells out of a board, exactly as in the diagram:

    row  0:  b b b b b b b b b b b b b b b b      (even rows: all b)
    row  1:  d b d b d b d b d b d b d b d b      (r = 1 mod 4: d at even cols)
    row  2:  b b b b b b b b b b b b b b b b
    row  3:  b d b d b d b d b d b d b d b d      (r = 3 mod 4: d at odd cols)
    ...

64 cells, no two of them adjacent (they are two apart along a row and the rows
that carry them are two apart), so every one of their four edges faces either a
b cell or the border.  The 192 b cells are one connected component: the even
rows are solid b, and every odd row's b cells hang off the even row above.

Because the holes are disjoint, the bipartite assignment in `complete` prices
every at-stake edge against a real, already-filled neighbour -- which is the
condition for it to be exactly optimal (the branch and bound is only needed
when two holes touch).
"""
import sys

W = H = 16


def dcells():
    out = []
    for r in range(H):
        if r % 2 == 0:
            continue                      # even rows are all b
        for c in range(W):
            # r = 1 mod 4 -> d on even columns;  r = 3 mod 4 -> d on odd columns
            if (c % 2 == 0) == (r % 4 == 1):
                out.append(r * W + c)
    return out


def load(path):
    pc, rt = [], []
    for l in open(path):
        l = l.strip()
        if not l or l.startswith('#'):
            continue
        a, b = l.split()
        pc.append(int(a))
        rt.append(int(b))
    return pc, rt


def main():
    src = sys.argv[1]
    pc, rt = load(src)
    cells = dcells()
    assert len(cells) == 64, len(cells)
    # sanity: no two adjacent
    for i, a in enumerate(cells):
        for b in cells[i + 1:]:
            dr = abs(a // W - b // W)
            dc = abs(a % W - b % W)
            assert dr + dc != 1, "holes touch"
    out = src.replace('.txt', '') + '.p64.txt'
    with open(out, 'w') as f:
        f.write(f"# {src}: the 64 disjoint 'd' cells punched out\n")
        for i in range(W * H):
            f.write(f"{-1 if i in cells else pc[i]} {rt[i]}\n")
    print(f"wrote {out}  (64 disjoint holes)")


if __name__ == '__main__':
    main()
