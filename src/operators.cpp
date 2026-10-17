// Eternity-Blossom -- matching-based operators (implementation).
//
// SPDX-License-Identifier: MIT
// See operators.hpp for the (long) explanation of why these two operators are
// non-bipartite and why that matters.

#include "e2/operators.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace e2 {
namespace {

double nowSeconds() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

// ===========================================================================
// (1) Optimal packet of disjoint swaps
// ===========================================================================
BlossomSwapResult BlossomSwapPass(Board& b, const BlossomSwapParams& pr, MatchingStats* ms) {
    BlossomSwapResult res;
    const Puzzle& pz = *b.pz;
    const auto t0 = std::chrono::steady_clock::now();

    // -- candidate cells: everything that currently touches a violated slot.
    std::vector<int> cand;
    cand.reserve(pz.ncells);
    for (int cell = 0; cell < pz.ncells; ++cell) {
        if (b.cellCostA[cell] > 0) cand.push_back(cell);
    }
    if ((int)cand.size() < pr.minCells) return res;

    // Cap the candidate set; keep the most violated cells.
    if ((int)cand.size() > pr.maxCells) {
        std::stable_sort(cand.begin(), cand.end(),
                         [&](int x, int y) { return b.cellCostA[x] > b.cellCostA[y]; });
        cand.resize(pr.maxCells);
        std::sort(cand.begin(), cand.end());
    }
    const int m = (int)cand.size();
    res.cells = m;
    res.ran = true;

    // -- candidate swaps with positive gain.
    //    gain(i,j) = -delta of swapping cells cand[i] and cand[j], best rotation.
    std::vector<WEdge> edges;
    std::vector<int> rotA, rotB;  // best rotations per offered edge
    const long long maxPairs = pr.maxPairs;
    long long considered = 0;
    for (int i = 0; i < m && (long long)edges.size() < maxPairs; ++i) {
        for (int j = i + 1; j < m && (long long)edges.size() < maxPairs; ++j) {
            ++considered;
            int ra = 0, rb = 0;
            const int d = b.bestSwapRotation(cand[i], cand[j], &ra, &rb);
            if (d < 0) {
                edges.emplace_back(i, j, -d);
                rotA.push_back(ra);
                rotB.push_back(rb);
            }
        }
    }
    res.pairsOffered = (int)edges.size();
    if (edges.empty()) { res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); return res; }

    // -- maximum-weight matching on the (non-bipartite) swap graph.
    std::vector<std::pair<int, int>> pairs;
    long long w = 0;
    if (!MaxWeightMatching(m, edges, pairs, w, ms)) {
        res.error = "matching solver failed";
        return res;
    }
    res.pairsChosen = (int)pairs.size();
    res.predictedGain = (int)w;
    if (pairs.empty()) { res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); return res; }

    // -- recover the gain of each chosen pair and sort by decreasing gain.
    std::unordered_map<long long, int> gainOf;
    gainOf.reserve(edges.size() * 2);
    for (const auto& [u, v, ww] : edges) gainOf[(long long)u * m + v] = ww;
    std::vector<std::array<int, 3>> chosen;  // {gain, i, j}
    chosen.reserve(pairs.size());
    for (const auto& [u, v] : pairs) {
        const int lo = std::min(u, v), hi = std::max(u, v);
        auto it = gainOf.find((long long)lo * m + hi);
        const int g = (it == gainOf.end()) ? 0 : it->second;
        if (g > 0) chosen.push_back({g, lo, hi});
    }
    std::sort(chosen.begin(), chosen.end(), [](const std::array<int, 3>& a, const std::array<int, 3>& b) {
        return a[0] > b[0];
    });

    // -- greedy filter: a swap changes the four (or fewer) slots around each of
    //    its two cells, so two swaps may only be applied together if no cell of
    //    one is a grid neighbour of a cell of the other.  Otherwise their gains
    //    are not additive and the matching has been optimising a surrogate.
    std::vector<char> blocked(pz.ncells, 0);
    std::vector<std::pair<int, int>> applyList;  // indices into cand
    if (pr.filterAdjacent) {
        for (const auto& arr : chosen) {
            const int i = arr[1], j = arr[2];
            const int u = cand[i], v = cand[j];
            if (blocked[u] || blocked[v]) continue;
            applyList.emplace_back(u, v);
            blocked[u] = blocked[v] = 1;
            for (int s = 0; s < 4; ++s) {
                if (pz.neigh[u * 4 + s] >= 0) blocked[pz.neigh[u * 4 + s]] = 1;
                if (pz.neigh[v * 4 + s] >= 0) blocked[pz.neigh[v * 4 + s]] = 1;
            }
        }
    } else {
        for (const auto& arr : chosen) applyList.emplace_back(cand[arr[1]], cand[arr[2]]);
    }
    if (applyList.empty()) { res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); return res; }

    // -- apply, measure the true delta, revert if it did not help.
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long before = b.cost;
    for (auto [u, v] : applyList) {
        // Re-optimise the rotations now: earlier swaps in the packet may have
        // changed the neighbourhood.
        int ra = 0, rb = 0;
        b.bestSwapRotation(u, v, &ra, &rb);
        b.applySwap(u, v, ra, rb);
    }
    const long long after = b.cost;
    res.costBefore = (int)before;
    res.costAfter = (int)after;
    res.actualGain = (int)(before - after);
    res.pairsApplied = (int)applyList.size();
    if (pr.acceptOnlyImproving && after >= before) {
        b.load(savedPc, savedRt);
        res.costAfter = (int)b.cost;
        res.actualGain = 0;
        res.pairsApplied = 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ===========================================================================
// (1b) Optimal reassignment of a set of cells
// ===========================================================================
AssignResult AssignReoptimizePass(Board& b, const AssignParams& pr, MatchingStats* ms) {
    AssignResult res;
    const Puzzle& pz = *b.pz;
    const auto t0 = std::chrono::steady_clock::now();
    const int n = std::min(std::max(2, pr.cells), pz.ncells);

    // ---- choose the cells: the most violated ones, with a little noise so
    //      repeated passes do not keep chewing on exactly the same set.
    std::vector<int> order(pz.ncells);
    std::iota(order.begin(), order.end(), 0);
    if (pr.useTopCells) {
        std::sort(order.begin(), order.end(), [&](int x, int y) {
            const int cx = b.cellCostA[x] * 8 + (x & 7);
            const int cy = b.cellCostA[y] * 8 + (y & 7);
            return cx > cy;
        });
    } else {
        std::shuffle(order.begin(), order.end(), std::mt19937(std::random_device{}()));
    }
    std::vector<int> cells(order.begin(), order.begin() + n);
    std::vector<int> pool(n);
    for (int i = 0; i < n; ++i) pool[i] = b.pc[cells[i]];
    res.cells = n;
    res.ran = true;

    // ---- the matching instance: cell i on the left, pool piece j on the right
    std::vector<WEdge> edges;
    edges.reserve((size_t)n * n);
    std::vector<int> bestRot((size_t)n * n, 0);
    for (int i = 0; i < n; ++i) {
        const int cell = cells[i];
        for (int j = 0; j < n; ++j) {
            const int p = pool[j];
            const int per = pz.pieces[p].period;
            int best = std::numeric_limits<int>::max(), br = 0;
            for (int r = 0; r < per; ++r) {
                const int v = b.cellCostIfA(cell, p, r);
                if (v < best) { best = v; br = r; }
            }
            edges.emplace_back(i, n + j, best);
            bestRot[(size_t)i * n + j] = br;
        }
    }
    std::vector<int> mate;
    long long w = 0;
    if (!MinWeightPerfectMatching(2 * n, edges, mate, w, ms)) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.predicted = (int)w;

    // ---- apply, measure, and keep only if it really helped
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long before = b.cost;
    long long changed = 0;
    for (int i = 0; i < n; ++i) {
        const int j = mate[i] - n;
        if (j < 0 || j >= n) continue;
        const int piece = pool[j];
        if (piece != b.pc[cells[i]]) ++changed;
        b.pc[cells[i]] = piece;
        b.rt[cells[i]] = bestRot[(size_t)i * n + j];
    }
    b.rebuild();
    const long long after = b.cost;
    res.costBefore = (int)before;
    res.costAfter = (int)after;
    res.actualGain = (int)(before - after);
    res.changed = changed;
    if (pr.acceptOnlyImproving && after >= before) {
        b.load(savedPc, savedRt);
        res.costAfter = (int)b.cost;
        res.actualGain = 0;
        res.changed = 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ===========================================================================
// (2) Stub-matching admissible bound
// ===========================================================================
void BuildRequirements(const Puzzle& pz, const Board& b, const std::vector<int>& cells,
                       const std::vector<char>& freeMark, std::vector<int>& req) {
    req.assign(cells.size() * 4, -1);
    for (size_t i = 0; i < cells.size(); ++i) {
        const int cell = cells[i];
        for (int s = 0; s < 4; ++s) {
            const int nb = pz.neigh[cell * 4 + s];
            int r = -1;
            if (nb < 0) r = kGray;                             // border slot
            else if (freeMark[nb]) r = -1;                     // faces another free cell
            else r = b.col[nb * 4 + oppositeSide(s)];          // faces a decided cell
            req[i * 4 + s] = r;
        }
    }
}

StubBoundResult StubMatchingBound(const Puzzle& pz, const std::vector<int>& cells,
                                  const std::vector<int>& pool, const std::vector<int>& req,
                                  MatchingStats* ms) {
    StubBoundResult res;
    res.heuristic = !MatchingEngineIsExact();
    const auto t0 = std::chrono::steady_clock::now();
    (void)cells;
    const int k = (int)pool.size();
    if (k == 0) { res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); return res; }

    // Demand vertices: one per slot that is already determined.
    std::vector<int> demandColor;
    for (size_t i = 0; i < req.size(); ++i) {
        if (req[i] >= 0) demandColor.push_back(req[i]);
    }
    const int nd = (int)demandColor.size();
    const int nStub = 4 * k;
    const int n = nStub + nd;

    if (n % 2 != 0) {
        // Inconsistent requirement (parity violation) -- cannot happen for a
        // consistent region, but be safe: the bound 0 is always admissible.
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    if (k == 1 && nd == 0) {
        // A single piece has to match itself -- illegal, and same-piece pairs
        // are excluded from the graph, so no perfect matching exists.
        res.infeasible = true;
        res.bound = INT32_MAX;
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // Vertex layout: [0, 4k) stubs (4 per piece), [4k, n) demands.
    // Stub colours: the piece's four edge colours, in any order -- a rotation
    // may put any of them on any side, which is exactly why no N/S vs E/W
    // typing exists and why the graph is non-bipartite.
    std::vector<int> stubColor(nStub);
    for (int i = 0; i < k; ++i) {
        const Piece& p = pz.pieces[pool[i]];
        for (int s = 0; s < 4; ++s) stubColor[4 * i + s] = p.c[s];
    }

    std::vector<WEdge> edges;
    const long long stubPairs = (long long)nStub * (nStub - 1) / 2;
    edges.reserve((size_t)std::min<long long>(stubPairs + (long long)nStub * nd, 4000000));
    // stub - stub (different pieces only: a piece never touches itself)
    for (int i = 0; i < k; ++i) {
        for (int a = 0; a < 4; ++a) {
            const int u = 4 * i + a;
            for (int j = i + 1; j < k; ++j) {
                for (int bb = 0; bb < 4; ++bb) {
                    const int v = 4 * j + bb;
                    edges.emplace_back(u, v, stubColor[u] == stubColor[v] ? 0 : 1);
                }
            }
        }
    }
    // stub - demand
    for (int u = 0; u < nStub; ++u) {
        for (int d = 0; d < nd; ++d) {
            edges.emplace_back(u, nStub + d, stubColor[u] == demandColor[d] ? 0 : 1);
        }
    }
    // No demand-demand edges: two determined slots never face each other.

    res.vertices = n;
    res.edges = (int)edges.size();
    std::vector<int> mate;
    long long w = 0;
    if (!MinWeightPerfectMatching(n, edges, mate, w, ms)) {
        res.infeasible = true;   // no perfect matching => the region is unsolvable
        res.bound = INT32_MAX;
    } else {
        res.bound = (int)w;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ===========================================================================
// (3) Exact region re-optimisation pruned by the stub bound
// ===========================================================================
namespace {

struct RegionSolver {
    const Puzzle& pz;
    const Board& board;
    const std::vector<int>& cells;
    std::vector<int> pool;
    std::vector<char> inRegion;
    std::vector<int> cellIndex;   // cell -> position in `cells`, or -1
    std::vector<int> placed;      // [i] piece id or -1
    std::vector<int> prot;
    std::vector<int> colOf;       // [i*4+s] colour of placed cell i side s, or -1
    std::vector<char> used;       // [pool index]
    std::vector<char> freeMark;
    int partial = 0;
    int best = 0;
    std::vector<int> bestPlaced, bestRot;
    long long nodes = 0, boundCalls = 0, boundPrunes = 0;
    const RegionParams& pr;
    MatchingStats* ms;
    std::vector<int> req;
    std::vector<int> remCells, remPool;

    RegionSolver(const Board& b, const std::vector<int>& cs, const RegionParams& p, MatchingStats* m)
        : pz(*b.pz), board(b), cells(cs), pr(p), ms(m) {
        inRegion.assign(pz.ncells, 0);
        cellIndex.assign(pz.ncells, -1);
        for (size_t i = 0; i < cells.size(); ++i) { inRegion[cells[i]] = 1; cellIndex[cells[i]] = (int)i; }
        pool.resize(cells.size());
        for (size_t i = 0; i < cells.size(); ++i) pool[i] = board.pc[cells[i]];
        placed.assign(cells.size(), -1);
        prot.assign(cells.size(), 0);
        colOf.assign(cells.size() * 4, -1);
        used.assign(pool.size(), 0);
        freeMark.assign(pz.ncells, 0);
        for (int c : cells) freeMark[c] = 1;
        req.resize(cells.size() * 4);
    }

    // Number of violated slots created by putting `piece`/`rot` on cells[idx],
    // counting slots against already-placed region cells, against fixed cells
    // outside the region, and against the border.  Weight 1 per violated slot,
    // matching the accounting used by the stub bound.
    int costOfPlacing(int idx, int piece, int rot) const {
        const int cell = cells[idx];
        int cost = 0;
        for (int s = 0; s < 4; ++s) {
            const int c = sideColor(pz.pieces[piece].c, s, rot);
            const int nb = pz.neigh[cell * 4 + s];
            if (nb < 0) {
                if (c != kGray) ++cost;
            } else if (inRegion[nb]) {
                const int j = cellIndex[nb];
                if (placed[j] >= 0 && c != colOf[j * 4 + oppositeSide(s)]) ++cost;
            } else {
                if (c != board.col[nb * 4 + oppositeSide(s)]) ++cost;
            }
        }
        return cost;
    }

    void place(int idx, int pi, int rot, int add) {
        placed[idx] = pool[pi];
        prot[idx] = rot;
        used[pi] = 1;
        for (int s = 0; s < 4; ++s) {
            colOf[idx * 4 + s] = sideColor(pz.pieces[pool[pi]].c, s, rot);
        }
        partial += add;
    }

    void unplace(int idx, int pi, int add) {
        placed[idx] = -1;
        prot[idx] = 0;
        used[pi] = 0;
        for (int s = 0; s < 4; ++s) colOf[idx * 4 + s] = -1;
        partial -= add;
    }

    // Admissible bound on the cost still to come, from the stub matching.
    int boundFor(int depth) {
        const int remaining = (int)cells.size() - depth;
        if (remaining == 0) return 0;
        if (!pr.useStubBound) return 0;
        if (depth > pr.boundMaxDepth && remaining > pr.useBoundAlwaysBelow) return 0;
        remCells.clear();
        for (int i = depth; i < (int)cells.size(); ++i) remCells.push_back(cells[i]);
        remPool.clear();
        for (size_t i = 0; i < pool.size(); ++i) if (!used[i]) remPool.push_back(pool[i]);
        if (remPool.size() != remCells.size()) return 0;   // should not happen
        // Requirements: for each remaining cell, each side.
        std::vector<int> r(remCells.size() * 4, -1);
        for (size_t i = 0; i < remCells.size(); ++i) {
            const int cell = remCells[i];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                int v = -1;
                if (nb < 0) v = kGray;
                else if (inRegion[nb]) {
                    const int j = cellIndex[nb];
                    if (placed[j] >= 0) v = colOf[j * 4 + oppositeSide(s)];
                    else v = -1;
                } else {
                    v = board.col[nb * 4 + oppositeSide(s)];
                }
                r[i * 4 + s] = v;
            }
        }
        ++boundCalls;
        StubBoundResult br = StubMatchingBound(pz, remCells, remPool, r, ms);
        if (br.infeasible) return INT32_MAX / 4;
        return br.bound;
    }

    void dfs(int depth) {
        if (nodes++ > pr.maxNodes) return;
        if (depth == (int)cells.size()) {
            if (partial < best) {
                best = partial;
                bestPlaced = placed;
                bestRot = prot;
            }
            return;
        }
        if (partial >= best) return;
        const int bnd = boundFor(depth);
        if (partial + bnd >= best) { ++boundPrunes; return; }

        const int idx = depth;
        // Order the remaining pieces: try the cheapest first for better pruning.
        std::vector<std::pair<int, int>> order;  // (add cost, pool index)
        order.reserve(pool.size());
        for (size_t pi = 0; pi < pool.size(); ++pi) {
            if (used[pi]) continue;
            const int per = pz.pieces[pool[pi]].period;
            int bestAdd = INT32_MAX;
            for (int r = 0; r < per; ++r) {
                bestAdd = std::min(bestAdd, costOfPlacing(idx, pi, r));
            }
            order.emplace_back(bestAdd, (int)pi);
        }
        std::sort(order.begin(), order.end());
        for (const auto& [ad, pi] : order) {
            if (partial + ad >= best) continue;   // this piece cannot help
            const int per = pz.pieces[pool[pi]].period;
            for (int r = 0; r < per; ++r) {
                const int add = costOfPlacing(idx, pi, r);
                if (partial + add >= best) continue;
                place(idx, pi, r, add);
                dfs(depth + 1);
                unplace(idx, pi, add);
                if (nodes > pr.maxNodes) return;
            }
            if (nodes > pr.maxNodes) return;
        }
    }
};

}  // namespace

RegionResult RegionReoptimize(Board& b, const std::vector<int>& cells, const RegionParams& pr,
                              MatchingStats* ms) {
    RegionResult res;
    const auto t0 = std::chrono::steady_clock::now();
    if (cells.empty() || (int)cells.size() > 14) {  // keep the exponential honest
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.ran = true;

    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long savedCost = b.cost;
    RegionSolver solver(b, cells, pr, ms);

    // Cost of the region as it stands, using the same 1-per-violated-slot
    // accounting that the DFS uses.  Every internal edge of the region is
    // counted exactly once, because costOfPlacing only charges slots whose
    // neighbour is already placed.
    int currentCost = 0;
    {
        RegionSolver probe(b, cells, pr, ms);
        // Simulate placing the current pieces in order; each internal edge is
        // counted exactly once because costOfPlacing only looks at already
        // placed neighbours.
        for (size_t i = 0; i < cells.size(); ++i) {
            const int piece = b.pc[cells[i]];
            const int rot = b.rt[cells[i]];
            int pi = -1;
            for (size_t j = 0; j < probe.pool.size(); ++j) {
                if (!probe.used[j] && probe.pool[j] == piece) { pi = (int)j; break; }
            }
            if (pi < 0) break;
            const int add = probe.costOfPlacing((int)i, pi, rot);
            probe.place((int)i, pi, rot, add);
        }
        currentCost = probe.partial;
    }

    solver.best = currentCost;
    solver.bestPlaced = solver.placed;
    solver.bestRot = solver.prot;
    solver.dfs(0);
    res.nodes = solver.nodes;
    res.boundCalls = solver.boundCalls;
    res.boundPrunes = solver.boundPrunes;
    res.costBefore = currentCost;
    res.costAfter = solver.best;

    if (solver.best < currentCost && !solver.bestPlaced.empty()) {
        // Apply the improved assignment and check the effect on the real board.
        for (size_t i = 0; i < cells.size(); ++i) {
            b.pc[cells[i]] = solver.bestPlaced[i];
            b.rt[cells[i]] = solver.bestRot[i];
        }
        b.rebuild();
        if (!pr.requireImprovement || b.cost < savedCost) {
            res.improved = true;
            res.costAfter = (int)b.cost;
        } else {
            b.load(savedPc, savedRt);
            res.improved = false;
            res.costAfter = currentCost;
        }
    } else {
        b.load(savedPc, savedRt);
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace e2
