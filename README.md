# Eternity-Blossom

**Eternity II attacked with Blossom VI minimum-weight perfect matching.**

Eternity II is a 16×16 edge-matching puzzle: 256 square pieces, 480 internal
adjacencies, all 64 border edges grey. Launched in 2007 with a $2M prize, never
solved. The best score anyone has published is **470 / 480**.

This repository is an attempt to push on it with a tool that is not normally
pointed at puzzles: an industrial-strength **minimum-weight perfect matching**
solver, [Blossom VI](https://github.com/Paul566/blossom-vi)
([arXiv:2604.20351](https://arxiv.org/abs/2604.20351)).

> **Read [DESIGN.md](DESIGN.md) before you read benchmark numbers.** The short
> version: Blossom cannot solve E2 (E2 is NP-hard, MWPM is polynomial — if the
> reduction existed we would have P = NP). What matching *does* give you are
> three polynomial-time operators that are provably optimal at what they do, two
> of which are genuinely non-bipartite and therefore impossible to express with
> Hungarian / min-cost-flow machinery. Those operators are implemented here and
> wired into a fast local search.

---

## The one thing that is genuinely new

```
./build/eternity-blossom rotbound --pieces data/e2pieces.txt --board results/best_441.txt
```

```
current       : 441/480 matched
  per-edge       465/480   (15 edges can never match at all)
  star (1 tile + neighbours, halved)  446/480
  strips of 4 rows     444/480
  best bound     444/480
  => rotations alone CANNOT solve this placement: at least 36 edges must stay broken.
```

A rigorous, polynomial-time (4 ms) ceiling on what a given placement of the
pieces can achieve by rotating them: partition the 480 internal edges into groups
that can each be solved **exactly** (strips by a pathwidth DP, seams for free
because they are vertex-disjoint) and relax only the constraints between groups.
On a random placement the same computation reports a ceiling of 145/480. Full
write-up in [experiments/MULTILEVEL.md](experiments/MULTILEVEL.md).

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/eternity-blossom selftest --pieces data/e2pieces.txt
./build/eternity-blossom solve  --pieces data/e2pieces.txt --time 60 --threads 4
```

Requirements: CMake ≥ 3.20, a C++20 compiler, and (optionally) Boost headers —
if Boost is absent the build uses a bundled `small_vector` shim. Blossom VI is
cloned automatically at configure time (or point CMake at a local checkout with
`-DBLOSSOM_VI_SOURCE_DIR=/path/to/blossom-vi`).

## Commands

| command | what it does |
|---------|--------------|
| `solve` | search for a high-scoring board (writes a board file) |
| `verify` | re-score a saved board from scratch |
| `bound` | compute the stub-matching lower bound at the root |
| `rotbound` | **upper bound on what rotation alone can reach** for a placement |
| `bench-subsolve` | compare the branch-and-bound lower bounds on identical problems |
| `dissect` | exact Hungarian reassignment over a library of dissections |
| `polish` | run the exact operators on an existing board |
| `selftest` | six correctness checks, each verified against brute force |

```
solve options
  --pieces FILE      piece file (default data/e2pieces.txt)
  --time SEC         wall clock limit (default 60)
  --threads N        independent searches, best board wins (default 1)
  --seed N           base RNG seed
  --out FILE         where to write the best board (default results/best_board.txt)
  --init MODE        greedy|structured|random|greedystruct (default greedystruct)
  --accept lahc|sa   late-acceptance hill climbing or simulated annealing
  --lahc N           LAHC history length (default 500)
  --t0 F --t1 F      SA start / end temperature
  --sample K         candidate proposals evaluated per move (default 1; 16 recommended)
  --targeted F       fraction of "aimed" moves (default 0.85)
  --assign N         cells per optimal-reassignment pass (0 disables)
  --noblossom        disable the Blossom swap-packet operator
  --region           enable exact region branch & bound pruned by the stub bound
  --wborder N        cost of a border violation relative to one mismatched edge
  --trace            print progress
  --json             machine-readable summary
```

Recommended starting point on the real puzzle:

```bash
./build/eternity-blossom solve --pieces data/e2pieces.txt \
    --time 300 --threads 8 --seed 1 \
    --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 \
    --out results/best.txt
```

`--sample 16` (best-of-16 proposals per move) and `--assign 256` (whole-board
optimal reassignment) are the two settings that matter most; between them they
are worth about 13 matched edges. See experiments/RESULTS.md §4.

## Input formats

**Pieces** — one piece per line, four integers, clockwise from North
(`N E S W`); colour `0` is grey:

```
0 0 1 17
0 0 1 5
...
```

`data/e2pieces.txt` is the authentic 256-piece set. It is in canonical sorted
form, so the piece numbering does not match the official printed numbering (and
therefore the "piece 139 starter clue" is not applicable to it) — but the piece
*multiset* is the real one, which is what the search needs. `Puzzle::verify`
checks the structural invariants on load: 64 grey edges, 4 corner pieces, 56
perimeter pieces, every non-grey colour with even multiplicity.

**Boards** — one `<piece> <rotation>` per cell, row-major, `#` comments:

```
# matched 432/480  border_bad 0  valid 1
12 3
207 1
...
```

## The matching operators

| operator | problem | graph | what it buys |
|---|---|---|---|
| **swap packet** (§3 of DESIGN) | best set of non-interfering swaps | **non-bipartite**: the complement of the grid graph | a move that is *provably optimal*, not just good |
| **stub bound** (§4) | admissible lower bound on mismatches of a partial board | **non-bipartite**: rotations destroy N/S vs E/W typing | branch & bound pruning, and a feasibility oracle |
| **reassignment** (§5) | best permutation of a pool over a set of cells | bipartite (Honest: Hungarian would be faster here) | escapes plateaus no swap can |
| **block-swap packet** (§5a) | best set of non-interfering *chunk* swaps | **non-bipartite**: complement of the coarse adjacency graph | the swap packet one level up |
| **rotation strips** (§5c) | optimal rotations for h consecutive rows | bipartite in the stub model; solved exactly by pathwidth DP | 32–64 rotation variables at once |
| **rotation bound** (§5c) | ceiling on what any rotation can reach | — (edge partition, not matching) | **proves a placement can't be finished** |

The first two and the block-swap packet are non-bipartite for a structural
reason, explained in detail in [DESIGN.md](DESIGN.md) — in one line each: *swaps
must not interfere, and the "must not interfere" graph is the complement of a
bipartite graph*; and *pieces may rotate freely, so a stub has no direction and
no 2-colouring exists*.

## Repository layout

```
include/e2/         public headers
  puzzle.hpp        pieces, rotations, board geometry, instance verification
  board.hpp         the incremental board (the hot loop; read its header comment)
  matching.hpp      the Blossom VI bridge + max-weight-matching reduction
  operators.hpp     the three matching operators
  search.hpp        LAHC / SA, restarts, parallelism
src/                implementations and the CLI
data/e2pieces.txt   the authentic 256-piece set
tools/              instance generator, board inspector, experiment runner
experiments/        measured numbers and how to reproduce them
third_party/        Boost shim (only used when Boost is absent)
```

## Testing

```bash
./build/eternity-blossom selftest --pieces data/e2pieces.txt
```

Three checks, all of which matter because everything else trusts them:

1. **matching vs brute force** — random graphs, Blossom VI's answer compared
   with an exact subset DP, checking both feasibility and optimality;
2. **incremental scoring** — 20 000 random swaps and rotations, checking the fast
   delta against the exact delta and both against a full rebuild;
3. **stub bound admissibility** — random 2×2 instances solved by brute force,
   checking the bound never exceeds the true optimum.

`ctest` runs the same thing.

## Licensing

**MIT** for everything in this repository — with one caveat you should actually
care about: the optional dependency **Blossom VI is GPL-3.0**. It is not
redistributed here (CMake clones it from upstream at configure time). If you
build against it, the resulting *binary* is a combined work and must be
distributed under the GPL-3.0; these sources remain MIT. For a pure-MIT build:

```bash
cmake -S . -B build -DE2_USE_BLOSSOM_VI=OFF
```

which falls back to the built-in matcher (exact subset DP for `n ≤ 18`, greedy +
2-opt above). Correct, but not fast enough for the 16×16 instance.

If you use Blossom VI, please cite
[arXiv:2604.20351](https://arxiv.org/abs/2604.20351).

## Status

See [experiments/RESULTS.md](experiments/RESULTS.md) for measured numbers.
Current best: **441/480** (300 s, 2 threads; verified by two independent
checkers — clean grey border, every piece used once). Typical single-core runs:
434/480 in 60 s, 429/480 in 25 s. The record is 470.
The gap is discussed honestly in both that file and
[DESIGN.md §8](DESIGN.md#8-results-and-how-far-this-is-from-the-record),
together with a ranked roadmap of what would actually close it.

## Dissection reassignment (bipartite / Hungarian)

`./build/eternity-blossom dissect --board results/best_441.txt --dissections 1000`

Dissects the board into groups (dominoes, L/Z shapes, rows, columns, squares), takes an
independent set of groups, freezes the rest and reassigns them exactly. Bipartite, so
Hungarian. On any board the searcher has already optimised it finds **exactly zero**
improvement, across 1057 dissections in 0.6 s -- a converged board is a fixed point of
this neighbourhood. From a random start it goes from 26/480 to 323/480 in 1.2 s.
Full write-up: [`experiments/DISSECTION.md`](experiments/DISSECTION.md).

## Why tile swapping is non-bipartite: a 2x3 worked example

[`experiments/blossom-2x3-example.html`](experiments/blossom-2x3-example.html) is a
self-contained page (open it directly in a browser) that works the swap-matching operator
end to end on a 2x3 window with real numbers: the original board, every swap gain, the
matching graph with all its weights, the two triangles that make it non-bipartite, why
Hungarian cannot be used, Blossom's answer verified against brute force, and the final
board. Regenerate the numbers with `./build/demo-2x3 --random`.

## The strong reassignment move: all 4^n * n! placements

`./build/eternity-blossom dissect --random --dissections 400 --full`

Instead of the 4 rigid quarter-turns, cost every (group, slot) pair by brute force over
all n! piece-to-cell assignments times all 4^n independent rotations: 32 placements for a
domino, 384 for a tromino, 6144 for a tetromino. Still bipartite, so still Hungarian.

Two consequences: a group's INTERNAL edges now change and must be costed, and a group no
longer has to match a slot's shape, only its size.

From an identical random start, run alone to convergence:

| dissection | placements/pair | score |
|---|---|---|
| dominoes, rigid rotation | 4 | 292 |
| **dominoes, full** | **32** | **346** |
| trominoes, full | 384 | 327 |
| tetrominoes, full | 6144 | 311 |
| 2x2 squares, full | 6144 | 273 |

So the strong move is worth **+54 edges** over the rigid one -- but **bigger polyominoes
are worse, not better**: pieces move in blocks of n, so one bad edge cannot be repaired
without disturbing n-1 others. Finer atoms beat more coverage.

On a board the searcher has already optimised it still finds exactly zero. Because the
move is exact this can be made a strong certificate: of 668 groups across dominoes,
trominoes and tetrominoes, **zero** can be improved even by rearranging within their own
cells. Full write-up: [`experiments/FULLREARRANGE.md`](experiments/FULLREARRANGE.md).
