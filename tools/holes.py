#!/usr/bin/env python3
"""Analyse a PARTIAL Eternity II board and solve the hole-filling subproblem.

Usage:
    python3 scratch/part/holes.py <board.txt> [--out FILE] [--limit N]

Reports:
  * how many cells are empty (holes), where they are, and how many hole-hole
    edges there are (the Hungarian completion in `complete` prices those
    against garbage, so it does not optimise them);
  * the mismatched count among FILLED-filled edges;
  * the number of edges "at stake" (edges touching at least one hole);
  * the EXACT optimum of the hole-filling subproblem (branch and bound), i.e.
    the best score any completion of this partial board can reach;
  * what the Hungarian completion would give, for comparison.
"""
import sys

W = H = 16
N = W * H
DR = (-1, 0, 1, 0)
DC = (0, 1, 0, -1)


def load_pieces(path):
    return [[int(x) for x in l.split()]
            for l in open(path) if l.strip() and not l.startswith('#')]


def load_board(path):
    pc, rt = [], []
    for l in open(path):
        l = l.strip()
        if not l or l.startswith('#'):
            continue
        a, b = l.split()
        pc.append(int(a))
        rt.append(int(b))
    assert len(pc) == N, f"board has {len(pc)} cells"
    return pc, rt


def nb_of(cell, side):
    r, c = divmod(cell, W)
    rr, cc = r + DR[side], c + DC[side]
    return rr * W + cc if (0 <= rr < H and 0 <= cc < W) else -1


def solve(pc, rt, pieces, node_limit=50_000_000):
    """Exact branch and bound over the holes.  Returns dict with results."""
    holes = [i for i in range(N) if pc[i] < 0]
    hset = set(holes)
    used = [False] * N
    for c in range(N):
        if pc[c] >= 0:
            used[pc[c]] = True
    unused = [p for p in range(N) if not used[p]]
    m = len(holes)
    assert m == len(unused)

    def colour(cell, side):
        return pieces[pc[cell]][(side - rt[cell]) % 4]

    # ---- statistics ---------------------------------------------------------
    ff_match = ff_miss = hh = hf = 0
    for cell in range(N):
        if pc[cell] < 0:
            continue
        for s in range(4):
            nb = nb_of(cell, s)
            if nb < 0 or nb < cell or pc[nb] < 0:
                continue
            if colour(cell, s) == colour(nb, (s + 2) % 4):
                ff_match += 1
            else:
                ff_miss += 1
    for cell in holes:
        for s in range(4):
            nb = nb_of(cell, s)
            if nb < 0:
                continue
            if nb in hset:
                if nb > cell:
                    hh += 1
            else:
                hf += 1

    # ---- precompute fixed gain: hole h, piece p, rotation r ------------------
    fixed = [[[0] * 4 for _ in range(N)] for _ in range(m)]
    for hi, cell in enumerate(holes):
        for p in range(N):
            for rot in range(4):
                g = 0
                for s in range(4):
                    nb = nb_of(cell, s)
                    col = pieces[p][(s - rot) % 4]
                    if nb < 0:
                        if col != 0:
                            g -= 1000          # border violation
                    elif nb not in hset:
                        if col == colour(nb, (s + 2) % 4):
                            g += 1
                fixed[hi][p][rot] = g

    # ---- hole-hole edges ----------------------------------------------------
    hhedges = []                       # (hi, hj, side_i, side_j)
    for hi, ci in enumerate(holes):
        for s in range(4):
            nb = nb_of(ci, s)
            if nb in hset:
                hj = holes.index(nb)
                if hj > hi:
                    hhedges.append((hi, hj, s, (s + 2) % 4))

    # ---- order: greedy, maximise already-ordered neighbours ------------------
    order, rem = [], list(range(m))
    while rem:
        best, bestv = rem[0], -1
        for h in rem:
            v = sum(1 for (a, b, _, _) in hhedges
                    if (a == h and b in order) or (b == h and a in order))
            if v > bestv:
                best, bestv = h, v
        order.append(best)
        rem.remove(best)
    pos = {h: i for i, h in enumerate(order)}

    # earlier[k] = edges from order[k] to holes earlier in the order
    earlier = [[] for _ in range(m)]
    for (a, b, sa, sb) in hhedges:
        if pos[a] < pos[b]:
            earlier[pos[b]].append((a, sa, sb))   # (other, other_side, my_side)
        else:
            earlier[pos[a]].append((b, sb, sa))
    # undecided hole-hole edges remaining when we are about to place order[k]
    hh_rem = [0] * (m + 1)
    for k in range(m - 1, -1, -1):
        hh_rem[k] = hh_rem[k + 1] + len(earlier[k])

    best = [-1]
    best_assign = [None]
    nodes = [0]
    over = [False]

    def dfs(k, gain, avail, assign):
        if over[0]:
            return
        nodes[0] += 1
        if nodes[0] > node_limit:
            over[0] = True
            return
        if k == m:
            if gain > best[0]:
                best[0] = gain
                best_assign[0] = list(assign)
            return
        # optimistic bound
        mx = max(max(fixed[h][p]) for p in avail) if avail else 0
        bound = gain + mx * (m - k) + hh_rem[k]
        if bound <= best[0]:
            return
        hi = order[k]
        for p in list(avail):
            avail.remove(p)
            for rot in range(4):
                g = fixed[hi][p][rot]
                for (hj, sj, si) in earlier[k]:
                    pj, rj = assign[hj]
                    if pieces[p][(si - rot) % 4] == pieces[pj][(sj - rj) % 4]:
                        g += 1
                assign[hi] = (p, rot)
                dfs(k + 1, gain + g, avail, assign)
                assign[hi] = None
            avail.append(p)

    dfs(0, 0, list(unused), [None] * m)

    out = {}
    if best_assign[0] is not None:
        out['fill'] = {holes[h]: best_assign[0][h] for h in range(m)}
    return dict(holes=holes, unused=unused, ff_match=ff_match, ff_miss=ff_miss,
                hh=hh, hf=hf, best_gain=best[0], nodes=nodes[0],
                over=over[0], order=order, piece_assignment=out.get('fill'))


def main():
    board_path = sys.argv[1]
    node_limit = 50_000_000
    if '--limit' in sys.argv:
        node_limit = int(sys.argv[sys.argv.index('--limit') + 1])
    pieces = load_pieces('data/e2pieces.txt')
    pc, rt = load_board(board_path)
    r = solve(pc, rt, pieces, node_limit)

    at_stake = r['hh'] + r['hf']
    print(f"board           : {board_path}")
    print(f"filled / empty  : {N - len(r['holes'])} / {len(r['holes'])}")
    print(f"holes           : {[(c // W, c % W) for c in r['holes']]}")
    print(f"hole-hole edges : {r['hh']}   hole-filled edges: {r['hf']}"
          f"   at stake: {at_stake}")
    print(f"filled-filled   : matched {r['ff_match']}  mismatched {r['ff_miss']}")
    print(f"exact fill      : gain {r['best_gain']}/{at_stake}"
          f"  -> board {r['ff_match'] + r['best_gain']}/480"
          f"{'  (NODE LIMIT)' if r['over'] else ''}")
    print(f"b&b nodes       : {r['nodes']}")

    if '--out' in sys.argv and r['piece_assignment']:
        out = sys.argv[sys.argv.index('--out') + 1]
        for cell, (p, rot) in r['piece_assignment'].items():
            pc[cell], rt[cell] = p, rot
        with open(out, 'w') as f:
            f.write("# Eternity-Blossom board\n# exact hole fill\n")
            for i in range(N):
                f.write(f"{pc[i]} {rt[i]}\n")
        print(f"wrote {out}")


if __name__ == '__main__':
    main()
