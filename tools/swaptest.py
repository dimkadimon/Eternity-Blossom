#!/usr/bin/env python3
"""How well can 64 ARBITRARY pieces fill the 64 disjoint d cells?

The user's idea stands or falls on this number.  Take a finished board, swap
the 64 pieces that happen to sit in the d cells with 64 randomly chosen pieces
from the b region, then punch the d cells out and let the (exactly optimal,
because the holes are disjoint) bipartite assignment refill them with those 64
arbitrary pieces.  The swap also scrambles the boundary colours the d cells see,
which is what a b region built without any regard for the d cells would look
like anyway.

usage: swaptest.py board.txt out.txt [seed]
"""
import random
import sys

W = H = 16


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
    src, out = sys.argv[1], sys.argv[2]
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    pc, rt = load(src)
    d = [(r, c) for r in range(H) for c in range(W)
         if r % 2 == 1 and (c % 2 == 0) == (r % 4 == 1)]
    b = [(r, c) for r in range(H) for c in range(W) if (r, c) not in set(d)]
    assert len(d) == 64 and len(b) == 192
    rng = random.Random(seed)
    pick = rng.sample(b, 64)
    for (r1, c1), (r2, c2) in zip(d, pick):
        i, j = r1 * W + c1, r2 * W + c2
        pc[i], pc[j] = pc[j], pc[i]
        rt[i], rt[j] = rt[j], rt[i]
    with open(out, 'w') as f:
        f.write(f"# {src}: 64 arbitrary pieces parked in the d cells, then punched\n")
        for i in range(W * H):
            r, c = divmod(i, W)
            f.write(f"{-1 if (r, c) in set(d) else pc[i]} {rt[i]}\n")
    print("wrote", out)


if __name__ == '__main__':
    main()
