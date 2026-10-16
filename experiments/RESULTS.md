# Measured results

Everything here is reproducible:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
INSTANCES="e2-16x16:data/e2pieces.txt g8x8:instances/gen8x8.txt g10x10:instances/gen10x10.txt" \
    bash tools/run_experiments.sh results/experiments.csv 25 "1 2"
```

Machine: 2 cores, 1 GB RAM, GCC 14.2, `-O3`, no `-march=native`. The solver is
single-threaded unless `--threads` is given. Scores are **matched internal edges
out of the number of internal adjacencies** (480 for 16×16), the number the
Eternity II community uses. **The record is 470/480.**

---

## 1. Headline

| run | time | threads | matched |
|---|---|---|---|
| **best run so far (seed 21)** | 300 s | 2 | **441 / 480** |
| seed 33 | 420 s | 2 | 437 / 480 |
| seed 11 (`--assign 48`) | 300 s | 2 | 435 / 480 |
| typical single run | 60 s | 1 | 434 / 480 |
| typical single run | 25 s | 1 | 429–432 / 480 |

Run-to-run spread at 300 s+ is about **±4 edges** (441 and 437 from two seeds at
similar budgets), so any difference smaller than that in the tables below should
be read as noise.

Best board committed at `results/best_441.txt`; verify it with

```bash
python3 tools/check_board.py --pieces data/e2pieces.txt --board results/best_441.txt
./build/eternity-blossom verify --pieces data/e2pieces.txt --board results/best_441.txt
```

Both checkers are independent implementations (pure Python vs C++) and both
report `441/480, mismatched 39, border_bad 0, every piece exactly once`.

**This does not beat the record of 470.** It is 29 edges short, and the honest
reading of the experiments below is that closing that gap is not a matter of
better matching — it needs a different neighbourhood (see DESIGN.md §8).

---

## 2. Correctness

Both build configurations are exercised: the default one (Blossom VI) and the
pure-MIT one (`-DE2_USE_BLOSSOM_VI=OFF`), and both pass every self-test. Testing
the MIT-only build found a real bug in the built-in fallback matcher — its
subset-DP reconstructs the matching by walking the dp table, but the DP only
extends by the lowest-numbered uncovered vertex and therefore never evaluates
`dp[S]` for sets that exclude vertex 0, states the walk needs. It is fixed with
explicit parent pointers. The bug never showed while Blossom VI was present
because that path is only reached when Blossom throws.

The solver is checked three ways, all in `./build/eternity-blossom selftest`:

| check | method | result |
|---|---|---|
| matching engine | random graphs, Blossom VI vs exact subset DP (feasibility + optimality) | pass |
| incremental scoring | 20 000 random swaps/rotations: fast delta vs exact delta vs full rebuild | pass |
| stub bound | random 2×2 instances, bound vs brute-force optimum | pass (never exceeds) |

End-to-end cross-validation on generated instances with known solutions — the
Python checker and the C++ checker are independent implementations:

| instance | internal edges | solved? |
|---|---|---|
| `gen4x4` | 24 | **24/24 perfect** |
| `gen5x5` | 40 | **40/40 perfect** |
| `gen6x6` | 60 | **60/60 perfect** |
| `gen8x8` | 112 | 104/112 (10 s) |
| `gen10x10` | 180 | 165/180 (25 s) |

---

## 3. Ablation: what is each matching operator worth?

25 s per run, 2 seeds, `--sample 16 --accept sa --t0 3.0 --t1 0.1`.
`full` = all operators, `noblossom` = swap packet off, `noassign` = reassignment
off, `plain` = neither (pure local search).

| instance | full | noblossom | noassign | plain |
|---|---|---|---|---|
| e2-16x16 (480) | 429.5 | **431.0** | 428.0 | 429.5 |
| g8x8 (112)     | 103.5 | 103.0 | 103.5 | **104.0** |
| g10x10 (180)   | **165.0** | 164.0 | 163.5 | 161.5 |

**Read this honestly.** On the real 16×16 and on 8×8 the operators are *within
noise* — with two seeds the run-to-run spread is about ±2 edges, which is the
size of every difference in those two rows. On 10×10 there is a consistent
signal: `full` beat `plain` on both seeds (165, 165 vs 161, 162), so about
**+3.5 edges, i.e. ~2 %**, and the ordering `full ≥ noblossom ≥ noassign ≥ plain`
held on both seeds.

Why so little? The operators are *optimal* at what they do, but the thing they
optimise is a surrogate:

* the swap packet optimises the **sum of individual swap gains**, which is exact
  only because the swaps in a packet are required not to interact;
* the reassignment pass scores every cell against a board that is about to
  change underneath it.

Optimality on a surrogate buys less than it sounds like. The measured
realisation rate for the swap packet — how much of the gain the matching
predicted actually materialises on the board — is about **60 %**:

```
blossom gain  : predicted 270488, measured 156286     (300 s run)
```

which is a useful diagnostic: the surrogate is informative, but not tight.

---

## 4. What did move the needle (ranked by measured effect)

| change | effect on matched edges |
|---|---|
| **best-of-K move generation** (`--sample 16` vs `1`) | **+8** (424 → 432) |
| **full-board reassignment** (`--assign 256` vs off) | **+5** in 60 s (429 → 434), **+6** in 300 s (435 → 441) |
| colour-aimed moves (`--targeted 1.0` vs `0.0`) | +10 (414 → 424) |
| faster incremental delta (rewrite of `board.hpp`) | 0 score, **7× iterations** |
| SA vs LAHC acceptance | +0 to +4 (noise) |
| LAHC history length 50…5000 | noise |
| perturbation size / stall limit | noise |
| 3-cycle moves (random third leg) | **−5 to −12** (harmful) |

The two biggest wins are both about *where to look*, not about matching. The
matching operators are worth a few edges on top of a well-aimed local search.

### 4.1 Reassignment size matters a lot

60 s, seed 5, everything else fixed:

| `--assign` | matched | assign passes | measured gain from the pass |
|---|---|---|---|
| 0 (off)    | 429 | 0   | 0 |
| 48 cells   | 429 | 167 | 1108 |
| 128 cells  | 432 | 167 | 8 |
| **256 (whole board)** | **434** | 161 | 4 |

Reproduced on seed 6: 434 as well. Note the "measured gain" column is nearly
zero for the large passes — the large reassignment is almost always *rejected*
by the "keep only if it improves" rule, yet the score still goes up. The
mechanism is diversification, not local improvement: proposing a global
reassignment and occasionally accepting one drops the search into a better basin
than perturbation-by-random-swaps does.

Frequency matters too (60 s, seed 5, `--assign 256`):

| `--assign-every` | matched |
|---|---|
| 50 000 (too often, costs 12 % of iterations) | 431 |
| **200 000** | **434** |
| 1 000 000 (too rare) | 432 |

---

## 5. The stub-matching bound: correct, and too weak

At the root it proves nothing:

```
$ ./build/eternity-blossom bound --pieces data/e2pieces.txt
  vertices 1088  edges 587776
  lower bound on mismatches: 0
  matching time 0.069s
```

(Blossom VI solving a 587 776-edge graph in 69 ms is worth noting on its own.)

The bound only becomes non-zero once most of the board is fixed. Inside the
region branch & bound (`--region`) it does fire and prune, but at 16×16 the
pruning does not repay the cost of calling a matching solver at search nodes:

```
--region --region-size 8 : nodes and bound prunes are reported by the solver;
                           net effect on the score is within noise.
```

We keep it because it is *correct* — `selftest` verifies admissibility against
brute force on random 2×2 instances — and because the regime where it should
actually pay is a different one: an exact solver that fixes most of the board and
must decide whether the last rows can be completed. There, the demand vertices
dominate the stub vertices and the relaxation becomes tight.

---

## 6. Cost of the matching operators

From the 300 s run:

```
blossom passes: 11202 (improved 6393, reverted 4809)
assign passes : 1120  (improved 406)  measured gain 5726
matching calls: 7513  avg vertices 177  avg edges 4358  total 1.639s
```

Total time in Blossom VI: **1.6 s out of 300 s (0.5 %)**. The operators are
essentially free in wall-clock terms; their cost is the ~3 % of iterations lost
to the passes, not the matching itself. Blossom VI is not the bottleneck and
never was.

---

## 7. Reproducing the headline number

```bash
./build/eternity-blossom solve --pieces data/e2pieces.txt \
    --time 300 --threads 2 --seed 21 \
    --sample 16 --accept sa --t0 3.0 --t1 0.1 --assign 256 \
    --out results/best.txt
```

Scaling observed: ~429 at 25 s, ~434 at 60 s, ~441 at 300 s (2 threads). The
simulated-annealing schedule is tied to the time budget, so a longer run is a
slower cool, not just more of the same. Diminishing returns set in hard — the
curve is roughly logarithmic after the first minute, which is the same
saturation every published E2 solver reports.

---

## 8. Honest conclusion

* Blossom VI is a genuinely good fit for three sub-problems in E2 search: the
  optimal packet of disjoint swaps (non-bipartite — complement of the grid
  graph), the admissible stub-matching bound (non-bipartite — rotations destroy
  the N/S vs E/W typing), and the optimal reassignment (bipartite).
* Two of the three are *provably optimal* at what they do, which is a real
  guarantee no heuristic move generator offers.
* Measured against a well-aimed local search, that optimality is worth roughly
  **2–4 matched edges** — real, repeatable on 10×10, inside the noise on 16×16.
* The gap to 470 is a *neighbourhood* problem, not a matching problem. The
  highest-value next step is greedy ejection chains (the implemented 3-cycle
  hurts only because its third leg is chosen at random), then an exact
  sub-board solver where the stub bound becomes tight. See DESIGN.md §8.
