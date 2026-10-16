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
| `selftest` | correctness checks for the matching engine, the incremental scoring and the bound |

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
    --time 300 --threads 8 --sample 16 --assign 48 --out results/best.txt
```

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

## The three matching operators

| operator | problem | graph | what it buys |
|---|---|---|---|
| **swap packet** (§3 of DESIGN) | best set of non-interfering swaps | **non-bipartite**: the complement of the grid graph | a move that is *provably optimal*, not just good |
| **stub bound** (§4) | admissible lower bound on mismatches of a partial board | **non-bipartite**: rotations destroy N/S vs E/W typing | branch & bound pruning, and a feasibility oracle |
| **reassignment** (§5) | best permutation of a pool over a set of cells | bipartite (Honest: Hungarian would be faster here) | escapes plateaus no swap can |

The first two are non-bipartite for a structural reason, explained in detail in
[DESIGN.md](DESIGN.md) — in one line each: *swaps must not interfere, and the
"must not interfere" graph is the complement of a bipartite graph*; and *pieces
may rotate freely, so a stub has no direction and no 2-colouring exists*.

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
Current best: **~432/480** in under a minute on one core. The record is 470.
The gap is discussed honestly in both that file and
[DESIGN.md §8](DESIGN.md#8-results-and-how-far-this-is-from-the-record),
together with a ranked roadmap of what would actually close it.
