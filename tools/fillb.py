#!/usr/bin/env python3
"""Fill only the 192 'b' cells of the diagram with a constructive search, and
leave the 64 disjoint 'd' cells empty.

The b cells are one connected component, and filling them row by row leaves
each cell with at most two already-filled b neighbours (the one above and the
one to the left) -- the other two sides face a d cell or an unfilled cell, and
so impose no constraint.  With 256 pieces available for 192 cells that is a
loosely constrained search, so a plain randomised greedy with restarts should
reach a zero-break filling; if it cannot, that is itself a result.

The 64 pieces it does not use are the ones the Hungarian assignment then has to
place, which is the real question this script exists to answer.

usage: fillb.py out.txt [attempts]
"""
import random
import sys

W = H = 16
GRAY = 0
# sides: 0=N 1=E 2=S 3=W   (dr,dc) = ((-1,0),(0,1),(1,0),(0,-1))
OPP = (2, 3, 0, 1)


def load_pieces(path):
    return [[int(x) for x in l.split()]
            for l in open(path) if l.strip() and not l.startswith('#')]


def is_d(r, c):
    if r % 2 == 0:
        return False
    return (c % 2 == 0) == (r % 4 == 1)


def main():
    out_path = sys.argv[1]
    attempts = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    pieces = load_pieces('data/e2pieces.txt')
    n = len(pieces)

    cells = [(r, c) for r in range(H) for c in range(W) if not is_d(r, c)]
    assert len(cells) == 192

    def nb(r, c, s):
        dr, dc = ((-1, 0), (0, 1), (1, 0), (0, -1))[s]
        rr, cc = r + dr, c + dc
        return (rr, cc) if 0 <= rr < H and 0 <= cc < W else None

    # colour of piece p on side s at rotation rot
    def col(p, rot, s):
        return pieces[p][(s - rot) % 4]

    # index (piece,rot) by the colour it shows on N and on W
    by_n = {}
    by_w = {}
    by_nw = {}
    for p in range(n):
        for rot in range(4):
            cn, cw = col(p, rot, 0), col(p, rot, 3)
            by_n.setdefault(cn, []).append((p, rot))
            by_w.setdefault(cw, []).append((p, rot))
            by_nw.setdefault((cn, cw), []).append((p, rot))

    # neighbour list per b cell: (side, other cell or None for border)
    nbrs = {}
    for (r, c) in cells:
        lst = []
        for s in range(4):
            o = nb(r, c, s)
            if o is None:
                lst.append((s, None))
            elif not is_d(*o):
                lst.append((s, o))
        nbrs[(r, c)] = lst

    best = None
    deepest = 0
    rng = random.Random(12345)
    for attempt in range(attempts):
        pc = [-1] * (W * H)
        rt = [0] * (W * H)
        used = [False] * n
        colour_at = {}          # (r,c,side) -> colour shown by the placed piece
        unfilled = set(cells)
        ok = True
        while unfilled:
            # most-constrained cell first: the one with the most constraints
            # already fixed (filled b neighbours + border slots), ties random
            bestr, bestc, bestv = None, None, -1
            for (r, c) in unfilled:
                v = 0
                for (s, o) in nbrs[(r, c)]:
                    if o is None or pc[o[0] * W + o[1]] >= 0:
                        v += 1
                if v > bestv:
                    bestr, bestc, bestv = r, c, v
            r, c = bestr, bestc
            need = {}                       # side -> required colour
            for (s, o) in nbrs[(r, c)]:
                if o is None:
                    need[s] = GRAY
                elif pc[o[0] * W + o[1]] >= 0:
                    need[s] = colour_at[(o[0], o[1], OPP[s])]
            # candidates: every unused piece/rotation meeting every requirement
            pool = []
            for p in range(n):
                if used[p]:
                    continue
                for ro in range(4):
                    if all(col(p, ro, s) == v for s, v in need.items()):
                        # do not waste a grey side on a side that faces a d
                        # cell or an unfilled b cell: the border needs 48 of
                        # the 64 grey sides and they are the scarce resource
                        waste = 0
                        for s in range(4):
                            if col(p, ro, s) == GRAY and s not in need:
                                waste += 1
                        pool.append((waste, p, ro))
            if not pool:
                ok = False
                break
            minwaste = min(pool)[0]
            pool = [x for x in pool if x[0] == minwaste]
            _, p, ro = pool[rng.randrange(len(pool))]
            used[p] = True
            pc[r * W + c] = p
            rt[r * W + c] = ro
            for s in range(4):
                colour_at[(r, c, s)] = col(p, ro, s)
            unfilled.discard((r, c))
        deepest = max(deepest, len(cells) - len(unfilled))
        if ok:
            best = (pc, rt)
            print(f"attempt {attempt}: filled all 192 b cells, 0 breaks")
            break

    if best is None:
        print(f"FAILED to fill the b region in {attempts} attempts "
              f"(best attempt placed {deepest}/192 cells)")
        return 1
    pc, rt = best
    leftover = [p for p in range(n) if not any(pc[r * W + c] == p for r, c in cells)]
    print(f"leftover pieces: {len(leftover)}")
    with open(out_path, 'w') as f:
        f.write("# b region filled constructively (0 breaks), 64 d cells empty\n")
        for i in range(W * H):
            f.write(f"{pc[i]} {rt[i]}\n")
    print("wrote", out_path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
