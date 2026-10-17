// Eternity-Blossom -- exact branch-and-bound over a sub-board.  See subsolve.hpp.
// SPDX-License-Identifier: MIT

#include "e2/subsolve.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include "e2/operators.hpp"

namespace e2 {

// ------------------------------------------------------------------ Hungarian
// Minimum-cost perfect assignment on a square cost matrix (O(n^3),
// shortest-augmenting-path / Jonker-Volgenant style).
static long long HungarianMin(const std::vector<std::vector<int>>& a) {
    const int n = (int)a.size();
    if (n == 0) return 0;
    const long long INF = std::numeric_limits<long long>::max() / 4;
    std::vector<long long> u(n + 1, 0), v(n + 1, 0), minv(n + 1);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::fill(minv.begin(), minv.end(), INF);
        std::vector<char> used(n + 1, 0);
        do {
            used[j0] = 1;
            const int i0 = p[j0];
            long long delta = INF;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const long long cur = (long long)a[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    long long total = 0;
    for (int j = 1; j <= n; ++j) total += a[p[j] - 1][j - 1];
    return total;
}

// -------------------------------------------------------------------- solver
namespace {

constexpr int kMaxColours = 64;

struct SubSolver {
    const Puzzle& pz;
    const Board& board;
    const std::vector<int>& cells;
    const std::vector<int>& pool;
    const SubSolveParams& pr;
    MatchingStats* ms;

    int n = 0, P = 0, wBorder = 8;
    std::vector<char> inRegion, used;
    std::vector<int> cellIndex, placed, prot, colOf, haveCol;
    int partial = 0, best = 0;
    std::vector<int> bestPlaced, bestRot;
    long long nodes = 0, edgePrunes = 0, assignPrunes = 0, stubPrunes = 0;
    long long stubCalls = 0, assignCalls = 0;
    bool aborted = false, timedOut = false, nodeLimited = false, infeas = false;
    std::chrono::steady_clock::time_point t0;

    SubSolver(const Board& b, const std::vector<int>& cs, const std::vector<int>& pl,
              const SubSolveParams& p, MatchingStats* m)
        : pz(*b.pz), board(b), cells(cs), pool(pl), pr(p), ms(m) {
        n = (int)cells.size();
        P = (int)pool.size();
        wBorder = b.wBorderRel;
        inRegion.assign(pz.ncells, 0);
        cellIndex.assign(pz.ncells, -1);
        for (int i = 0; i < n; ++i) { inRegion[cells[i]] = 1; cellIndex[cells[i]] = i; }
        placed.assign(n, -1);
        prot.assign(n, 0);
        colOf.assign((size_t)n * 4, -1);
        used.assign(P, 0);
        haveCol.assign(kMaxColours, 0);
        for (int i = 0; i < P; ++i) bumpCol(pool[i], +1);
        t0 = std::chrono::steady_clock::now();
    }

    void bumpCol(int piece, int delta) {
        bool seen[kMaxColours] = {false};
        for (int s = 0; s < 4; ++s) {
            const int c = pz.pieces[piece].c[s];
            if (c >= 0 && c < kMaxColours && !seen[c]) { seen[c] = true; haveCol[c] += delta; }
        }
    }

    // Violations created by putting pool[pi] at cells[idx] in rotation rot.
    // Units: 1 per mismatched internal edge, wBorder per bad border slot.
    int costOfPlacing(int idx, int pi, int rot) const {
        const int cell = cells[idx];
        const int piece = pool[pi];
        int cost = 0;
        for (int s = 0; s < 4; ++s) {
            const int c = sideColor(pz.pieces[piece].c, s, rot);
            const int nb = pz.neigh[cell * 4 + s];
            if (nb < 0) {
                if (c != kGray) cost += wBorder;
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
        bumpCol(pool[pi], -1);
        for (int s = 0; s < 4; ++s) colOf[idx * 4 + s] = sideColor(pz.pieces[pool[pi]].c, s, rot);
        partial += add;
    }

    void unplace(int idx, int pi, int add) {
        bumpCol(pool[pi], +1);
        placed[idx] = -1;
        prot[idx] = 0;
        used[pi] = 0;
        for (int s = 0; s < 4; ++s) colOf[idx * 4 + s] = -1;
        partial -= add;
    }

    // Cost of the region as it stands, in the same units.
    int currentCost() {
        int total = 0;
        std::vector<char> saveUsed = used;
        std::vector<int> savePlaced = placed, saveProt = prot, saveCol = colOf;
        std::vector<int> saveHave = haveCol;
        int savePartial = partial;
        for (int i = 0; i < n; ++i) {
            int pi = -1;
            for (int j = 0; j < P; ++j)
                if (!used[j] && pool[j] == board.pc[cells[i]]) { pi = j; break; }
            if (pi < 0) break;
            const int add = costOfPlacing(i, pi, board.rt[cells[i]]);
            place(i, pi, board.rt[cells[i]], add);
        }
        total = partial;
        used = saveUsed;
        placed = savePlaced;
        prot = saveProt;
        colOf = saveCol;
        haveCol = saveHave;
        partial = savePartial;
        return total;
    }

    // ---- bound (1): slots no remaining piece can satisfy
    int edgeBound(int idx) {
        int h = 0;
        for (int i = idx; i < n; ++i) {
            const int cell = cells[i];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                int target;
                if (nb < 0) {
                    target = kGray;
                } else if (inRegion[nb]) {
                    const int j = cellIndex[nb];
                    if (placed[j] < 0) continue;  // not decided yet
                    target = colOf[j * 4 + oppositeSide(s)];
                } else {
                    target = board.col[nb * 4 + oppositeSide(s)];
                }
                if (target >= 0 && target < kMaxColours && haveCol[target] == 0)
                    h += (nb < 0) ? wBorder : 1;
            }
        }
        return h;
    }

    // ---- bound (2): rectangular assignment over the remaining cells
    int assignBound(int idx) {
        const int m = n - idx;
        int nFree = 0;
        for (int i = 0; i < P; ++i) if (!used[i]) ++nFree;
        if (m == 0 || nFree < m) return 0;
        // rows 0..m-1 are the remaining cells, the rest are zero-cost dummies
        // that absorb the surplus pool pieces.
        std::vector<std::vector<int>> a((size_t)nFree, std::vector<int>(nFree, 0));
        for (int i = 0; i < m; ++i) {
            const int idx2 = idx + i;
            int col = 0;
            for (int pi = 0; pi < P; ++pi) {
                if (used[pi]) continue;
                const int per = pz.pieces[pool[pi]].period;
                int v = std::numeric_limits<int>::max() / 4;
                for (int rot = 0; rot < per; ++rot) v = std::min(v, costOfPlacing(idx2, pi, rot));
                a[i][col] = v;
                ++col;
            }
        }
        ++assignCalls;
        return (int)HungarianMin(a);
    }

    // ---- bound (3): the non-bipartite stub matching bound
    int stubBound(int idx) {
        std::vector<int> remCells(cells.begin() + idx, cells.end());
        std::vector<int> remPool;
        remPool.reserve(P);
        for (int i = 0; i < P; ++i)
            if (!used[i]) remPool.push_back(pool[i]);
        if (remPool.size() != remCells.size()) return 0;  // stub model needs 1 piece per cell
        std::vector<int> req(remCells.size() * 4, -1);
        for (size_t i = 0; i < remCells.size(); ++i) {
            const int cell = remCells[i];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                int val = -1;
                if (nb < 0) val = kGray;
                else if (inRegion[nb]) {
                    const int j = cellIndex[nb];
                    if (placed[j] >= 0) val = colOf[j * 4 + oppositeSide(s)];
                } else {
                    val = board.col[nb * 4 + oppositeSide(s)];
                }
                req[i * 4 + s] = val;
            }
        }
        const StubBoundResult r = StubMatchingBound(pz, remCells, remPool, req, ms);
        if (r.heuristic) return 0;   // not a bound: the engine is not exact
        if (r.infeasible) {
            infeas = true;
            return std::numeric_limits<int>::max() / 4;
        }
        return r.bound;
    }

    void dfs(int idx) {
        if (aborted) return;
        ++nodes;
        if ((nodes & 255) == 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >
                pr.timeLimitSeconds) {
            aborted = true;
            timedOut = true;
            return;
        }
        if (nodes > pr.maxNodes) {
            aborted = true;
            nodeLimited = true;
            return;
        }
        const int rem = n - idx;
        if (rem == 0) {
            if (partial < best) {
                best = partial;
                bestPlaced = placed;
                bestRot = prot;
            }
            return;
        }
        if (pr.useEdgeBound) {
            const int h = edgeBound(idx);
            if (partial + h >= best) { ++edgePrunes; return; }
        }
        if (pr.useAssignBound && rem <= pr.assignMaxCells) {
            const int h = assignBound(idx);
            if (partial + h >= best) { ++assignPrunes; return; }
        }
        const int stubMin = pr.stubAutoShallow ? std::max(pr.stubMinCells, n - 2)
                                               : pr.stubMinCells;
        if (pr.useStubBound && rem <= pr.stubMaxCells && rem >= stubMin &&
            stubCalls < pr.maxStubCalls) {
            ++stubCalls;
            const int h = stubBound(idx);
            if (partial + h >= best) { ++stubPrunes; return; }
        }

        struct Cand { int cost, pi, rot; };
        std::vector<Cand> cands;
        cands.reserve((size_t)(P - idx) * 4);
        for (int pi = 0; pi < P; ++pi) {
            if (used[pi]) continue;
            const int per = pz.pieces[pool[pi]].period;
            for (int rot = 0; rot < per; ++rot) {
                const int c = costOfPlacing(idx, pi, rot);
                if (partial + c < best) cands.push_back({c, pi, rot});
            }
        }
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& x, const Cand& y) { return x.cost < y.cost; });
        for (const Cand& cd : cands) {
            place(idx, cd.pi, cd.rot, cd.cost);
            dfs(idx + 1);
            unplace(idx, cd.pi, cd.cost);
            if (aborted) return;
        }
    }
};

}  // namespace

std::vector<int> RectRegion(const Puzzle& pz, int r0, int c0, int h, int w) {
    std::vector<int> out;
    for (int r = r0; r < r0 + h; ++r)
        for (int c = c0; c < c0 + w; ++c)
            if (r >= 0 && r < pz.H && c >= 0 && c < pz.W) out.push_back(r * pz.W + c);
    return out;
}

SubSolveResult SubSolveRegion(Board& b, const std::vector<int>& cells,
                              const std::vector<int>& pool, const SubSolveParams& pr,
                              MatchingStats* ms) {
    SubSolveResult res;
    const auto t0 = std::chrono::steady_clock::now();
    if (cells.empty() || (int)pool.size() < (int)cells.size()) return res;
    res.ran = true;
    res.cells = (int)cells.size();
    res.poolSize = (int)pool.size();

    SubSolver s(b, cells, pool, pr, ms);
    const int cur = s.currentCost();
    const int startBest = pr.proveOptimal ? (std::numeric_limits<int>::max() / 4)
                                          : (cur + (pr.keepEqual ? 1 : 0));
    s.best = startBest;
    s.dfs(0);

    res.exact = !s.aborted;
    res.nodes = s.nodes;
    res.edgePrunes = s.edgePrunes;
    res.assignPrunes = s.assignPrunes;
    res.stubPrunes = s.stubPrunes;
    res.stubCalls = s.stubCalls;
    res.assignCalls = s.assignCalls;
    res.infeasiblePrune = s.infeas;
    res.timedOut = s.timedOut;
    res.nodeLimited = s.nodeLimited;
    res.regionBefore = cur;
    res.regionAfter = (s.best >= startBest && pr.proveOptimal) ? cur : s.best;

    if (!s.bestPlaced.empty()) {
        const std::vector<int> savedPc = b.pc, savedRt = b.rt;
        const long long savedCost = b.cost;
        for (int i = 0; i < s.n; ++i) {
            b.pc[cells[i]] = s.bestPlaced[i];
            b.rt[cells[i]] = s.bestRot[i];
        }
        b.rebuild();
        res.realBefore = savedCost;
        res.realAfter = b.cost;
        if (pr.requireImprovement && b.cost >= savedCost) {
            b.load(savedPc, savedRt);
            res.improved = false;
            res.realAfter = savedCost;
        } else {
            res.improved = true;
        }
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace e2
