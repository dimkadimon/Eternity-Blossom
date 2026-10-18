#!/usr/bin/env python3
"""Convert a board exported by Blackwood's solver into Eternity-Blossom format.

Rotation.  Eternity-Blossom presents, on side s, the colour c[(s - rot) & 3], so
its rotation count is the number of clockwise quarter turns.  Blackwood's
Rotations==1 has TopSide = piece.LeftSide (new top = old left), also one
clockwise quarter turn.  Comparing the two piece files pin it down exactly:
Blackwood's piece 1 unrotated is (T,R,B,L) = (1,17,0,0) and Eternity-Blossom's
piece 0 at rotation 2 is (N,E,S,W) = (1,17,0,0).  The side labels correspond
directly (his T,R,B,L are our N,E,S,W), so

        our_rotation = (blackwood_rotation + 2) & 3

Numbering.  Blackwood numbers pieces 1..256, Eternity-Blossom 0..255, so our
index is his minus one.  Verified on pieces 1, 2 and 139.  His 0 (no piece)
becomes -1.

Orientation.  The side labels are NOT flipped, but the board may be, because the
two solvers may number rows from opposite ends.  All four are offered; the
`complete` tool settles it -- only the right one puts the grey edges on the
border (border_bad 0).
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
mode = "id"
for a in sys.argv[3:]:
    mode = a.lstrip("-")

rows = []
with open(src) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p, r = line.split()
        p = int(p)
        rows.append((-1 if p == 0 else p - 1, int(r)))
assert len(rows) == 256, f"expected 256 cells, got {len(rows)}"

out = []
for i in range(256):
    br, bc = divmod(i, 16)
    if mode == "id":
        j = br * 16 + bc
    elif mode == "vflip":
        j = (15 - br) * 16 + bc
    elif mode == "hflip":
        j = br * 16 + (15 - bc)
    elif mode == "rot180":
        j = (15 - br) * 16 + (15 - bc)
    else:
        raise SystemExit(f"unknown mode {mode}")
    p, r = rows[j]
    out.append((p, (r + 2) & 3))

empty = sum(1 for p, _ in out if p < 0)
with open(dst, "w") as f:
    f.write("# Eternity-Blossom board\n")
    f.write(f"# converted from Blackwood export, mode={mode}, empty cells {empty}\n")
    for p, r in out:
        f.write(f"{p} {r}\n")
print(f"wrote {dst}  mode={mode}  empty={empty}")
