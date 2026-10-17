// Eternity-Blossom -- 2-factor cycle cover and matching-guided ruin & recreate.
// See nmoves.hpp.  SPDX-License-Identifier: MIT

#include "e2/nmoves.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <random>

#include "e2/assign.hpp"

namespace e2 {

namespace {

int manhattan(const Puzzle& pz, int a, int c) {
    const int ra = a / pz.W, ca = a % pz.W;
    const int rb = c / pz.W, cb = c % pz.W;
    return (ra > rb ? ra - rb : rb - ra) + (ca > cb ? ca - cb : cb - ca);
}

void shortestPath(const Puzzle& pz, int a, int c, std::vector<int>& out) {
    out.clear();
    int r = a / pz.W, col = a % pz.W;
    const int rt = c / pz.W, ct = c % pz.W;
    out.push_back(r * pz.W + col);
    while (r != rt) { r += (rt > r) ? 1 : -1; out.push_back(r * pz.W + col); }
    while (col != ct) { col += (ct > col) ? 1 : -1; out.push_back(r * pz.W + col); }
}

// Least cost of putting the piece currently in cell `from` into cell `to`, over
// the four rotations, measured against the target values as they stand.
int shiftCost(const Board& b, int from, int to) {
    const Puzzle& pz = *b.pz;
    const int piece = b.pc[from];
    int best = INT_MAX;
    for (int r = 0; r < 4; ++r) {
        int c = 0;
        for (int s = 0; s < 4; ++s) {
            const int base = to * 4 + s;
            const int nb = pz.neigh[base];
            const int wv = (nb < 0) ? b.wBorderSlot : 2;
            const int nc = (int)b.protCol[piece * 16 + r * 4 + s];
            if (nc != (int)b.target[base]) c += wv;
        }
        best = std::min(best, c);
    }
    return best;
}

// Rotate the pieces one step along `cycle` (or the other way if `reverse`),
// giving each landing cell the best rotation for the piece arriving in it.
void cyclicShift(Board& b, const std::vector<int>& cycle, bool reverse) {
    const Puzzle& pz = *b.pz;
    std::vector<int> np = b.pc, nr = b.rt;
    const int L = (int)cycle.size();
    for (int i = 0; i < L; ++i) {
        const int from = cycle[reverse ? ((i + 1) % L) : ((i + L - 1) % L)];
        const int to = cycle[i];
        int bestR = 0, bestC = INT_MAX;
        for (int r = 0; r < 4; ++r) {
            int c = 0;
            for (int s = 0; s < 4; ++s) {
                const int base = to * 4 + s;
                const int nb = pz.neigh[base];
                const int wv = (nb < 0) ? b.wBorderSlot : 2;
                const int nc = (int)b.protCol[b.pc[from] * 16 + r * 4 + s];
                if (nc != (int)b.target[base]) c += wv;
            }
            if (c < bestC) { bestC = c; bestR = r; }
        }
        np[to] = b.pc[from];
        nr[to] = bestR;
    }
    for (int cell : cycle) { b.pc[cell] = np[cell]; b.rt[cell] = nr[cell]; }
    b.rebuild();
}

}  // namespace

// ------------------------------------------------------------------ 2-factor

bool PerfectTwoMatching(const Puzzle& pz, const std::vector<int>& cells,
                        const std::vector<std::vector<int>>& w,
                        std::vector<std::vector<int>>& cycles,
                        MatchingStats* ms) {
    (void)pz;
    cycles.clear();
    const int k = (int)cells.size();
    if (k < 3) return false;

    // ---- Tutte's gadget.  Two copies v0 = 2i, v1 = 2i+1 of each vertex; every
    //      edge {i,j} becomes a K2,2 between the copies.  A perfect matching
    //      here is a perfect 2-matching back in the original graph, i.e. a
    //      spanning collection of cycles -- possibly with 2-cycles where the
    //      matching takes both copies of the same edge.
    //
    //      The gadget is NON-BIPARTITE whenever the original graph has a
    //      triangle: for vertices a,b,c pairwise adjacent, a0-b0, b0-c0 and
    //      c0-a0 are all gadget edges, so a0 b0 c0 is an odd cycle.  Our cell
    //      graph is complete, so this always applies and Blossom is required.
    const int n = 2 * k;
    std::vector<WEdge> edges;
    edges.reserve((size_t)4 * k * (k - 1) / 2);
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j)
            for (int a = 0; a < 2; ++a)
                for (int bb = 0; bb < 2; ++bb)
                    edges.emplace_back(2 * i + a, 2 * j + bb, w[i][j]);

    std::vector<int> mate;
    long long wt = 0;
    if (!MinWeightPerfectMatching(n, edges, mate, wt, ms)) return false;

    // ---- read the 2-matching off: count how many of the four gadget edges
    //      between i and j the matching used (0, 1 or 2).
    std::vector<std::vector<int>> mult((size_t)k, std::vector<int>(k, 0));
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j) {
            int c = 0;
            if (mate[2 * i] / 2 == j) ++c;
            if (mate[2 * i + 1] / 2 == j) ++c;
            mult[i][j] = mult[j][i] = c;
        }

    // ---- walk the 2-regular multigraph into cycles
    std::vector<char> done(k, 0);
    for (int start = 0; start < k; ++start) {
        if (done[start]) continue;
        int deg = 0;
        for (int j = 0; j < k; ++j) deg += mult[start][j];
        if (deg != 2) continue;

        // Walk TRANSACTIONALLY: build the cycle on a scratch copy of the
        // multiplicities and only commit it if the walk completes.  Committing
        // as we go meant a walk that failed its checks had already consumed
        // edges and marked cells done, and those cells then vanished from the
        // cover -- which turns the "rotation" into a move that loses pieces.
        // The self-test [10/12] caught this.
        auto scratch = mult;
        std::vector<int> cyc;
        std::vector<char> inCyc(k, 0);
        int v = start, prev = -1;
        bool ok = true;
        while (true) {
            cyc.push_back(v);
            inCyc[v] = 1;
            // Two passes: take a fresh edge if there is one, and only fall back
            // to the edge we arrived on when there is nothing else.  Without the
            // fallback a 2-CYCLE never closes -- at the second vertex the only
            // remaining edge is the one back to the start, and a 2-cycle is a
            // legitimate outcome of the gadget (both copies of one edge taken,
            // i.e. a transposition).  Without the preference, a triangle would
            // close as a premature 2-cycle instead of running its full length.
            int nxt = -1;
            for (int j = 0; j < k; ++j)
                if (scratch[v][j] > 0 && j != prev) { nxt = j; break; }
            if (nxt < 0)
                for (int j = 0; j < k; ++j)
                    if (scratch[v][j] > 0) { nxt = j; break; }
            if (nxt < 0) { ok = false; break; }
            scratch[v][nxt]--;
            scratch[nxt][v]--;
            if (nxt == start) break;               // closed the cycle
            if (inCyc[nxt]) { ok = false; break; } // would revisit a cell
            prev = v;
            v = nxt;
            if ((int)cyc.size() > k) { ok = false; break; }
        }
        if (!ok || cyc.size() < 2) { done[start] = 1; continue; }

        mult = scratch;
        for (int idx : cyc) done[idx] = 1;
        std::vector<int> real;
        for (int idx : cyc) real.push_back(cells[idx]);
        cycles.push_back(real);
    }
    return true;
}

CycleCoverResult CycleCoverPass(Board& b, const CycleCoverParams& pr, MatchingStats* ms) {
    CycleCoverResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const Puzzle& pz = *b.pz;

    // ---- choose the cell set S
    std::vector<int> cells;
    if (pr.set == "all") {
        for (int c = 0; c < pz.ncells; ++c) cells.push_back(c);
    } else if (pr.set == "sample") {
        std::mt19937 rng(12345);
        for (int c = 0; c < pz.ncells; ++c) cells.push_back(c);
        std::shuffle(cells.begin(), cells.end(), rng);
        cells.resize(std::min((int)cells.size(), pr.maxCells));
    } else {
        std::vector<char> inS(pz.ncells, 0);
        for (int c = 0; c < pz.ncells; ++c)
            if (b.cellCostA[c] > 0) { inS[c] = 1; }
        if (pr.set == "unhappy-hood") {
            std::vector<char> grow = inS;
            for (int c = 0; c < pz.ncells; ++c) {
                if (!inS[c]) continue;
                for (int s = 0; s < 4; ++s) {
                    const int nb = pz.neigh[c * 4 + s];
                    if (nb >= 0) grow[nb] = 1;
                }
            }
            inS = grow;
        }
        for (int c = 0; c < pz.ncells; ++c) if (inS[c]) cells.push_back(c);
        if ((int)cells.size() > pr.maxCells) {
            std::stable_sort(cells.begin(), cells.end(),
                             [&](int x, int y) { return b.cellCostA[x] > b.cellCostA[y]; });
            cells.resize(pr.maxCells);
            std::sort(cells.begin(), cells.end());
        }
    }
    // a 2-factor needs an even number of vertices in the gadget, always true,
    // but a perfect 2-matching needs |S| >= 3
    if ((int)cells.size() > pr.maxCells) cells.resize(pr.maxCells);
    res.cells = (int)cells.size();
    if (res.cells < 3) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.ran = true;

    // ---- weights: an undirected cycle will be traversed one way or the other,
    //      so charge the cheaper of the two directions for each adjacency.
    const int k = res.cells;
    std::vector<std::vector<int>> w((size_t)k, std::vector<int>(k, 0));
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j) {
            const int cij = shiftCost(b, cells[i], cells[j]);
            const int cji = shiftCost(b, cells[j], cells[i]);
            w[i][j] = w[j][i] = std::min(cij, cji);
        }

    std::vector<std::vector<int>> cycles;
    if (!PerfectTwoMatching(pz, cells, w, cycles, ms)) {
        res.error = "2-factor matching failed";
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.cycles = (int)cycles.size();
    if (cycles.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- rotate along each cycle, both directions, keep what helps
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long before = b.cost;
    long long bestCost = before;
    std::vector<int> bestPc = b.pc, bestRt = b.rt;
    int applied = 0, moved = 0;

    for (const auto& cyc : cycles) {
        if (applied >= pr.maxCycles) break;
        if ((int)cyc.size() > pr.maxCycleLen) continue;

        b.load(bestPc, bestRt);
        cyclicShift(b, cyc, false);
        const long long cF = b.cost;
        const std::vector<int> pcF = b.pc, rtF = b.rt;

        b.load(bestPc, bestRt);
        cyclicShift(b, cyc, true);
        const long long cR = b.cost;

        if (cF <= cR && cF < bestCost) { b.load(pcF, rtF); }
        else if (cR < cF && cR < bestCost) { /* already loaded */ }
        else { b.load(bestPc, bestRt); continue; }

        bestCost = b.cost;
        bestPc = b.pc;
        bestRt = b.rt;
        ++applied;
        moved += (int)cyc.size();
    }
    b.load(bestPc, bestRt);
    res.applied = applied;
    res.movedCells = moved;
    res.costBefore = before;
    res.costAfter = b.cost;
    res.actualGain = before - b.cost;
    if (pr.acceptOnlyImproving && b.cost >= before) {
        b.load(savedPc, savedRt);
        res.costAfter = before;
        res.actualGain = 0;
    } else {
        res.improved = res.actualGain > 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// --------------------------------------------------------------- blast (LNS)

BlastResult BlastPass(Board& b, const BlastParams& pr, MatchingStats* ms) {
    BlastResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const Puzzle& pz = *b.pz;

    // ---- terminals: the unhappy cells
    std::vector<int> term;
    for (int c = 0; c < pz.ncells; ++c)
        if (b.cellCostA[c] > 0) term.push_back(c);
    const int maxT = 2 * pr.maxPairs;
    if ((int)term.size() > maxT) {
        std::stable_sort(term.begin(), term.end(),
                         [&](int x, int y) { return b.cellCostA[x] > b.cellCostA[y]; });
        term.resize(maxT);
        std::sort(term.begin(), term.end());
    }
    res.terminals = (int)term.size();
    if (res.terminals < 2) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.ran = true;

    // ---- maximum-weight matching on the complete graph over the terminals,
    //      non-bipartite: short links preferred, so the ruin set is compact.
    //      (Cardinality-free, so the solver may leave a terminal unmatched if
    //      pairing it would drag in a long path.)
    const int k = res.terminals;
    // IMPORTANT: MaxWeightMatching is cardinality-FREE, so with all-negative
    // weights its optimum is the empty matching and the ruin set comes out
    // empty.  Shift every weight by (max distance + 1): then each weight is
    // positive, so taking more pairs is always worthwhile (up to a perfect
    // matching), and among matchings of a given size the short links win.
    int maxD = 1;
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j) maxD = std::max(maxD, manhattan(pz, term[i], term[j]));
    const int shift = maxD + 1;
    std::vector<WEdge> edges;
    edges.reserve((size_t)k * (k - 1) / 2);
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j)
            edges.emplace_back(i, j, shift - manhattan(pz, term[i], term[j]));
    std::vector<std::pair<int, int>> pairs;
    long long pw = 0;
    if (!MaxWeightMatching(k, edges, pairs, pw, ms)) {
        res.error = "matching failed";
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- ruin set = union of the shortest paths between matched pairs
    std::vector<char> inR(pz.ncells, 0);
    int np = 0;
    for (auto& pr2 : pairs) {
        if (np >= pr.maxPaths) break;
        std::vector<int> path;
        shortestPath(pz, term[pr2.first], term[pr2.second], path);
        if ((int)path.size() > pr.maxPathLen) continue;
        for (int c : path) inR[c] = 1;
        ++np;
    }
    res.paths = np;
    std::vector<int> ruined;
    for (int c = 0; c < pz.ncells; ++c) if (inR[c]) ruined.push_back(c);
    if ((int)ruined.size() > pr.maxCells) {
        std::shuffle(ruined.begin(), ruined.end(), std::mt19937(7));
        std::sort(ruined.begin(), ruined.begin() + pr.maxCells);
        ruined.resize(pr.maxCells);
    }
    res.ruined = (int)ruined.size();
    if (res.ruined < 2) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- recreate: reassign the pieces that were in the ruined cells back to
    //      those cells, exactly and in their best rotation.  Bipartite, so
    //      HungarianMin, and provably optimal for this ruin set.
    const int m = res.ruined;
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long before = b.cost;

    std::vector<std::vector<int>> cm((size_t)m, std::vector<int>(m, 0));
    std::vector<std::vector<int>> bestRot((size_t)m, std::vector<int>(m, 0));
    for (int i = 0; i < m; ++i) {
        const int piece = b.pc[ruined[i]];
        for (int j = 0; j < m; ++j) {
            int best = INT_MAX, br = 0;
            for (int r = 0; r < 4; ++r) {
                int c = 0;
                for (int s = 0; s < 4; ++s) {
                    const int base = ruined[j] * 4 + s;
                    const int nb = pz.neigh[base];
                    const int wv = (nb < 0) ? b.wBorderSlot : 2;
                    const int nc = (int)b.protCol[piece * 16 + r * 4 + s];
                    if (nc != (int)b.target[base]) c += wv;
                }
                if (c < best) { best = c; br = r; }
            }
            cm[i][j] = best;
            bestRot[i][j] = br;
        }
    }
    std::vector<int> colForRow;
    HungarianMin(cm, &colForRow);

    // Local cost of the ruin set before the repair, so the prediction can be
    // compared with what the board actually delivered.
    long long oldLocal = 0;
    for (int c : ruined) oldLocal += b.cellCostA[c];

    for (int i = 0; i < m; ++i) {
        b.pc[ruined[colForRow[i]]] = savedPc[ruined[i]];
        b.rt[ruined[colForRow[i]]] = bestRot[i][colForRow[i]];
    }
    b.rebuild();

    long long newLocal = 0;
    for (int c : ruined) newLocal += b.cellCostA[c];

    res.costBefore = before;
    res.costAfter = b.cost;
    res.actualGain = before - b.cost;
    // The assignment's own optimum is a LOCAL figure: it ignores that a cell
    // outside the ruin set may share an edge with one inside.  The board's cost
    // is the number that counts, so report both rather than conflating them --
    // that conflation was the blossom-gain bug in commit 9715a3c.
    res.predictedGain = oldLocal - newLocal;

    if (pr.acceptOnlyImproving && b.cost >= before) {
        b.load(savedPc, savedRt);
        res.costAfter = before;
        res.actualGain = 0;
        res.improved = false;
    } else {
        res.improved = res.actualGain > 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace e2
