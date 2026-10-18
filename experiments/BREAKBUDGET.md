# The break budget: the knob nobody was turning

`Util.cs:15` in Blackwood's C# solver:

```csharp
private static List<int> break_indexes_allowed = new List<int>() { 201, 206, 211, 216, 221, 225, 229, 233, 237, 239 };
```

Ten entries, hard-coded, not exposed on the command line.  It is the budget for
"place a piece that does not match the neighbours it touches": `Get_Break_Array()`
turns it into a cumulative allowance over the search order, and a piece whose
`Break_Count` exceeds the allowance at that index is skipped.  Every partial
board we had ever seen carried exactly 10 mismatched edges among its filled
cells — the budget was being spent in full, every time, and we had assumed it
was a property of the search rather than a parameter.

## The sweep

`SECS` seconds, 2 threads, `--export` the deepest board, convert, then fill the
remaining cells with `complete` (or `verify`, if it finished).

| budget | deepest index | cells left | score |
|---|---|---|---|
| 0 | 217 | 39 | 417 |
| 4 | 233 | 23 | 445 |
| 10 (shipped) | 248 | 8 | 458–461 |
| 12 | 252 | 4 | 460–462 |
| **13** | **254** | **2** | **462–466** |
| 14 | 254 | 2 | 465 |
| 15 | 254 | 2 | 463 |
| 16 | **256** | **0** | **464** |
| 20 | 256 | 0 | 460 |
| 24 | 256 | 0 | 456 |

**16 or more completes the board outright.**  A completion that used *k* breaks
scores 480−*k* with no fill at all, which is a genuinely different pipeline from
"fill 248 cells and hope for the last 8" — the fill's ~45% hit rate is removed
from the calculation entirely.  It is also slower to arrive at than it looks:
the solver returns the *first* completion it finds, and it spends the whole
budget getting there, so a budget of 20 scores 460, not 470.

**13 is the sweet spot**: 254 of 256 cells, two cells for the fill, 466/480
after it — equal to our best board, from a single 120-second DFS run with no
polish and no simulated annealing.

Five independent runs at budget 13, 120 s each: **463, 463, 463, 462, 464**
(mean 463.0).  Two earlier runs gave 466.  Polishing and annealing those boards
buys about half an edge (463→464, 464→464, 466→466).

## Why the curve is flat, and why that is the real lesson

The marginal trade is almost exactly break-even:

* one more break costs **1** matched edge, directly;
* one more break buys about **2 more filled cells** here, and each cell filled
  by the DFS instead of the fill keeps ~2 edges out of the fill's hands,
  which at the fill's ~45% hit rate is worth about **1.1** edges.

So the whole budget range lands in a 462–466 band, and the shipped value of 10
was already close to optimal.  The DFS is not leaving depth on the table: at
budget 13 it stalls at index 254 whether you give it 120 s or 300 s, so the
limit is the search, not the clock.

The two ends of the curve are where the real cost is.  At budget 0 the search
stalls at 217 cells and the fill, handed 78 edges, returns 417.  At budget 24 it
completes easily and throws away 24 edges.  This is the same arithmetic as
`DISJOINT_HOLES.md`, from the other direction: **the fill converts edges to
matched edges at ~45% and the DFS at ~98%, so the interesting quantity is how
many edges each stage is responsible for — and the break budget is just a dial
that moves edges between them at parity.**

## Consequence

Nothing here beats 466, but budget 13 is a *cheaper* generator of 462–466
boards than the old pipeline (120 s, one stage, no annealing, versus
40 s DFS + fill + polish + 150 s SA).  It also produces structurally different
boards — 2 holes instead of 8, breaks at different places — which is the
diversity the memetic layer in `CROSSOVER.md` was missing when it converged on
a population of 16 near-identical boards.

## Reproducing

Needs the .NET SDK, which installs outside the workspace so it does not count
against the 128 MB snapshot budget:

```bash
export TMPDIR=/var/tmp
bash ~/dotnet-install.sh --channel 8.0 --install-dir /usr/local/dotnet --no-path
export DOTNET_ROOT=/usr/local/dotnet PATH=/usr/local/dotnet:$PATH
```

then

```bash
SECS=120 BUDGETS="12 13 14 16" bash ~/EternityII_Solver/breaksweep.sh
bash ~/EternityII_Solver/breakexp.sh          # the 0 / 4 / 10 / 16 corners
```

Both scripts patch `Util.cs`, rebuild, run, convert the export with
`scratch/bw2e2.py … vflip`, and score it.  Remember to restore `Util.cs`
afterwards:

```csharp
private static List<int> break_indexes_allowed = new List<int>() { 201, 206, 211, 216, 221, 225, 229, 233, 237, 239 };
```

## Result

A second, structurally distinct **466/480** board, verified by both
implementations, at `results/best_466_b13.txt`.
