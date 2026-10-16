#!/usr/bin/env python3
"""Generate solvable edge-matching instances with a known solution.

The generator lays down a random solved board first and then reads the pieces off
it, so the instance is guaranteed to have at least one perfect solution (useful
for checking that a solver really can reach 480, and for measuring how far it
stops short).

Colour 0 is always grey and is only used on the border, exactly as in Eternity II.

usage:
    python3 tools/gen_instance.py --size 6 --colors 8 --seed 1 --out inst6.txt
"""
import argparse
import random


def generate(size: int, ncolors: int, seed: int):
    """Return (pieces, solution_board).

    solution_board[cell] = (piece_id, rotation) -- a perfect solution.
    pieces[i] = [N, E, S, W] colours of piece i.
    """
    rng = random.Random(seed)
    n = size * size
    # Random colour for every internal adjacency, grey for every border slot.
    # slot colours are stored as [cell][side].
    slot = [[0] * 4 for _ in range(n)]
    for r in range(size):
        for c in range(size):
            cell = r * size + c
            for s, (dr, dc) in enumerate(((-1, 0), (0, 1), (1, 0), (0, -1))):
                rr, cc = r + dr, c + dc
                if 0 <= rr < size and 0 <= cc < size:
                    # internal: decide the colour once, from the lower-numbered side
                    if cell < rr * size + cc:
                        col = rng.randint(1, ncolors - 1)
                        slot[cell][s] = col
                        slot[rr * size + cc][(s + 2) % 4] = col
                else:
                    slot[cell][s] = 0  # grey border

    pieces = [slot[cell][:] for cell in range(n)]
    # Randomly rotate each piece; the solution stores the rotation that undoes it.
    sol = []
    for cell in range(n):
        rot = rng.randrange(4)
        # rotating the piece clockwise by `rot` puts c[(s-rot)%4] on side s
        pieces[cell] = [slot[cell][(s - rot) % 4] for s in range(4)]
        sol.append((cell, (4 - rot) % 4))
    # Shuffle the piece order so piece id carries no information.
    order = list(range(n))
    rng.shuffle(order)
    inv = [0] * n
    for new_id, old_id in enumerate(order):
        inv[old_id] = new_id
    shuffled = [None] * n
    for old_id in range(n):
        shuffled[inv[old_id]] = pieces[old_id]
    solution = [(inv[p], r) for (p, r) in sol]
    return shuffled, solution


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=6)
    ap.add_argument("--colors", type=int, default=8,
                    help="number of colours including grey (default 8)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default="instance.txt")
    ap.add_argument("--solution", default="", help="optional path for the known solution")
    args = ap.parse_args()

    pieces, solution = generate(args.size, args.colors, args.seed)
    with open(args.out, "w") as f:
        for p in pieces:
            f.write(" ".join(map(str, p)) + "\n")
    print(f"wrote {args.out}: {args.size}x{args.size}, {len(pieces)} pieces, "
          f"{args.colors} colours (incl. grey)")
    if args.solution:
        with open(args.solution, "w") as f:
            for p, r in solution:
                f.write(f"{p} {r}\n")
        print(f"wrote {args.solution} (a perfect solution)")


if __name__ == "__main__":
    main()
