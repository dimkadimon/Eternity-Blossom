#!/usr/bin/env python3
"""The 84-cell 'b' pattern: geometry, and what it costs to fill by assignment.

Every scheme of the form "fill most of the board by search, then place the rest
by optimal assignment" lives or dies on one number: how many edges the
assignment is made responsible for.  The search matches ~98% of the edges it
decides; the fill matches ~45% of the edges it is handed.  So the score is
roughly  480 - 0.02*(search edges) - 0.55*(edges at stake),  and anything with
more than about 25 edges at stake cannot reach 466.

This prints the geometry of a pattern and the resulting projection.
"""
import re
import sys

PATTERN = """
b a b a b a b a b a b a b a b a
a a a a a a a a a a a a a a a a
b a b a b a b a b a b a b a b a
a a a b a b a b a b a b a a a b
b a b a a a a a a a a a b a b a
a a a b a b a b a b a b a a a a
b a b a a a b a a a b a b a b a
a a a b a b a b a b a a a b a b
b a b a b a a a a a b a b a a a
a a a a a a a b a b a a a b a b
b a b a b a b a b a b a b a a a
a a a b a a a a a a a a a b a b
b a a a b a b a b a b a b a a a
a b a b a b a b a b a b a b a b
a a a a a a a a a a a a a a a a
a b a b a b a b a b a b a b a b
"""

# Rate at which the exact fill matches an edge it is responsible for, measured
# repeatedly this session (16 real partial boards: 5-9 of 12-18; 64 arbitrary
# pieces swapped into the d cells: 109-115 of 240).
FILL_RATE = 0.46


def parse(text):
    rows = []
    for line in text.strip().split("\n"):
        cells = line.split()
        if cells:
            assert len(cells) == 16, f"row has {len(cells)} cells: {line}"
            rows.append(cells)
    assert len(rows) == 16, f"{len(rows)} rows"
    return rows


def main():
    rows = parse(PATTERN)
    N = 16
    holes = [(r, c) for r in range(N) for c in range(N) if rows[r][c] == "b"]
    isb = [[rows[r][c] == "b" for c in range(N)] for r in range(N)]

    # Walk every internal edge once and classify it by what it touches.
    hh = hf = 0            # hole-hole, hole-filled internal edges
    for r in range(N):
        for c in range(N):
            for (r2, c2) in ((r, c + 1), (r + 1, c)):
                if r2 >= N or c2 >= N:
                    continue
                if isb[r][c] and isb[r2][c2]:
                    hh += 1
                elif isb[r][c] or isb[r2][c2]:
                    hf += 1

    # border slots: the sides of b cells that face off the board.  These must
    # come out grey, which the fill has to respect.
    border = 0
    for (r, c) in holes:
        if r == 0:
            border += 1
        if r == N - 1:
            border += 1
        if c == 0:
            border += 1
        if c == N - 1:
            border += 1

    # sanity: every b cell has four sides, each either an internal edge or a
    # border slot, and a b-b edge uses up two of them.
    assert 2 * hh + hf + border == 4 * len(holes), (hh, hf, border, len(holes))

    at_stake = hh + hf
    filled = N * N - len(holes)
    search_edges = 480 - at_stake

    print(f"cells               : {len(holes)} b, {filled} a")
    print(f"border-edge cells   : {border} of the b cells touch the board edge")
    print(f"edges at stake      : {at_stake}   (b-b {hh}, b-a {hf})")
    print(f"edges the search owns: {search_edges}")
    print()
    # a-b adjacency: how many b cells are isolated
    nbrs = [sum(1 for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0))
                if 0 <= r + dr < N and 0 <= c + dc < N and isb[r + dr][c + dc])
            for (r, c) in holes]
    from collections import Counter
    print("b-cell neighbour counts:", sorted(Counter(nbrs).items()))
    print()
    proj_search_bad = round(0.02 * search_edges)
    proj_fill_bad = round((1 - FILL_RATE) * at_stake)
    print(f"projection: search mismatches ~{proj_search_bad}, "
          f"fill mismatches ~{proj_fill_bad}")
    print(f"projected score     : ~{480 - proj_search_bad - proj_fill_bad} / 480")
    print()
    print("For comparison, the 64-cell d pattern had 240 at stake and the")
    print("present pipeline (8 clustered holes) has 16.")


if __name__ == "__main__":
    main()
