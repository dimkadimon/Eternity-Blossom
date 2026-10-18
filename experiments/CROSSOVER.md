# Crossing the hybrid boards, and what the SA acceptance rule is worth

Two things were tried once the hole fill was exact: giving the memetic layer the
hybrid pipeline's boards to cross, and re-tuning the SA that finishes them.
Both are measured here.  The short version: crossover lifts every board to the
level of the best one and then stops, and the cooler SA schedule is worth about
a quarter of an edge.

## Seeding the population (`pop --boards DIR`)

The published 467–470 results all use population search with crossover, and
`pop` already had it — but it could only cross the offspring of its *own* random
restarts, which are ~20 edges worse than a board the hybrid pipeline produces.
So the crossover never had good material.  `--boards DIR` loads every `*.txt`
in a directory and uses them as the first individuals verbatim (they have
already had their local search, which is the point); leftover slots are filled
by independent runs as before.

```bash
./build/eternity-blossom pop --boards scratch/part/popseeds --pop 16 \
    --time 900 --trace --sample 16 --assign 256
```

Seeded with the 16 polished boards (460–465, best 465):

```
gen 1   best 30 (465/480)  mean 35
gen 2   best 30 (465/480)  mean 33
gen 3   best 30 (465/480)  mean 32
gen 4   best 30 (465/480)  mean 30
...
gen 11  best 30 (465/480)  mean 30     150 crossovers, 19 improvements
child cost BEFORE refinement: mean 80   best 30   (best parent 30)
```

So the layer does exactly one thing, and does it well: it drags every board up
to the best board's level — the whole population sits at 465 after four
generations — and then it has nowhere to go.  The best child *before* refinement
is already as good as the best parent, so the transplant-plus-Hungarian-repair
is not the problem; the 6 s refinement at `t0 = 0.30` simply cannot find
anything past the parents.  Eleven generations and 150 crossovers produced no
board better than the best seed.

That is consistent with everything else: these boards are deep local optima
(see `HOLEFILL.md` — re-solving any 16 cells exactly changes nothing), and
crossover between two of them lands back in the same basin.

## The SA acceptance rule, paired

Same 8 polished boards, same seeds, one thing changed each time (150 s,
2 threads, `--sample 16 --assign 256 --nmove-repair`):

| acceptance | board scores | mean |
|---|---|---|
| SA, t0 3.0 → t1 0.1 (pipeline default) | 462 465 464 464 462 462 463 466 | 463.50 |
| LAHC (solver default) | 461 465 464 464 461 462 461 465 | 462.88 |
| **SA, t0 1.5 → t1 0.05** | **463 465 464 464 462 463 463 466** | **463.75** |

The cooler schedule is best, and paired against the default it gains +1 on two
boards and loses on none.  The margin is small against the noise (sd ≈ 1.24, so
8 boards give se ≈ 0.44), so treat it as "probably a little better", not as a
new plateau.  It is the schedule `chase2.sh` uses.

**Trap worth writing down:** `--accept` defaults to `lahc`, and `--t0/--t1`
only apply to `sa`.  Passing temperatures without `--accept sa` silently gives
you LAHC — which is what produced two identical-looking "different" sweeps
before it was spotted.

## Where this leaves the chase

Neither the exact fill, nor crossover, nor the acceptance rule moves the mean
much: everything lands in 462–466.  What is left is the spread, so
`scratch/part/chase2.sh` samples it — 16 boards × 8 seeds, resumable (every
finished run is appended to a log and re-running skips it), so it can be
interrupted and relaunched:

```bash
setsid nohup bash scratch/part/chase2.sh >> scratch/part/chase2.out 2>&1 &
```

At mean 463.9 and sd 1.24, a 467 needs roughly a 2.5σ draw — about 1 sample in
170, so ~120 runs (240 SA trajectories at 2 threads) is a coin flip and a few
hundred is comfortable.  That is hours of wall clock, not minutes.
