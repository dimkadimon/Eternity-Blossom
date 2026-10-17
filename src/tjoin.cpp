// Eternity-Blossom -- T-join moves.  See tjoin.hpp.
// SPDX-License-Identifier: MIT

#include "e2/tjoin.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <queue>

namespace e2 {

namespace {

int manhattan(const Puzzle& pz, int a, int c) {
    const int ra = a / pz.W, ca = a % pz.W;
    const int rb = c / pz.W, cb = c % pz.W;
    return (ra > rb ? ra - rb : rb - ra) + (ca > cb ? ca - cb : cb - ca);
}

// An L-shaped shortest grid path from `a` to `c` (row first, then column).
// Any shortest path will do; the join only needs the edges.
void shortestPath(const Puzzle& pz, int a, int c, std::vector<int>& out) {
    out.clear();
    int r = a / pz.W, col = a % pz.W;
    const int rt = c / pz.W, ct = c % pz.W;
    out.push_back(r * pz.W + col);
    while (r != rt) { r += (rt > r) ? 1 : -1; out.push_back(r * pz.W + col); }
    while (col != ct) { col += (ct > col) ? 1 : -1; out.push_back(r * pz.W + col); }
}

}  // namespace

bool TJoinChains(const Puzzle& pz, const std::vector<int>& terminals,
                 std::vector<std::vector<int>>& chains, MatchingStats* ms) {
    chains.clear();
    const int k = (int)terminals.size();
    if (k < 2 || (k & 1)) return false;

    // ---- minimum-weight perfect matching on the complete graph over T, with
    //      weight = -(grid distance).  Blossom VI, non-bipartite.
    std::vector<WEdge> edges;
    edges.reserve((size_t)k * (k - 1) / 2);
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j)
            edges.emplace_back(i, j, -manhattan(pz, terminals[i], terminals[j]));

    std::vector<int> mate;
    long long mw = 0;
    if (!MinWeightPerfectMatching(k, edges, mate, mw, ms)) return false;

    // ---- union of the shortest paths, with cancellation: an edge used twice
    //      cancels, so only edges used an ODD number of times survive.  That
    //      surviving set is exactly the T-join, and its odd-degree vertices are
    //      exactly T (every interior vertex of a path contributes degree 2).
    std::map<std::pair<int, int>, int> cnt;
    for (int i = 0; i < k; ++i) {
        if (mate[i] <= i) continue;
        std::vector<int> path;
        shortestPath(pz, terminals[i], terminals[mate[i]], path);
        for (size_t t = 0; t + 1 < path.size(); ++t) {
            const int u = path[t], v = path[t + 1];
            cnt[{std::min(u, v), std::max(u, v)}]++;
        }
    }
    std::vector<std::pair<int, int>> join;
    for (auto& kv : cnt)
        if (kv.second % 2 == 1) join.push_back(kv.first);
    if (join.empty()) return true;

    // ---- adjacency with explicit edge ids, so the walk can consume each once
    std::vector<std::vector<std::pair<int, int>>> adj(pz.ncells);
    for (size_t e = 0; e < join.size(); ++e) {
        adj[join[e].first].push_back({join[e].second, (int)e});
        adj[join[e].second].push_back({join[e].first, (int)e});
    }
    std::vector<char> used(join.size(), 0);

    // ---- start from the odd-degree vertices (the terminals), walk out
    std::vector<int> order = terminals;
    for (int cell = 0; cell < pz.ncells; ++cell)
        if (!adj[cell].empty() && std::find(order.begin(), order.end(), cell) == order.end())
            order.push_back(cell);

    for (int start : order) {
        // does this vertex still have a free edge?
        bool any = false;
        for (auto& e : adj[start]) if (!used[e.second]) any = true;
        if (!any) continue;

        // A cyclic shift is only a permutation if the chain visits each cell at
        // most once, so stop the walk the moment it would revisit one.  (A
        // degree-4 vertex can be entered twice, and the naive walk produced
        // chains with a repeated cell -- which silently duplicated a piece and
        // lost another.  The self-test caught it.)
        std::vector<int> chain;
        std::vector<char> inChain(pz.ncells, 0);
        int v = start;
        while (true) {
            chain.push_back(v);
            inChain[v] = 1;
            int nxt = -1, eid = -1;
            for (auto& e : adj[v])
                if (!used[e.second]) { nxt = e.first; eid = e.second; break; }
            if (nxt < 0) break;
            if (nxt == start) { used[eid] = 1; break; }   // closed a cycle
            if (inChain[nxt]) break;                      // would revisit
            used[eid] = 1;
            v = nxt;
        }
        if (chain.size() >= 2) chains.push_back(chain);
    }
    return true;
}

TJoinResult TJoinPass(Board& b, const TJoinParams& pr, MatchingStats* ms) {
    TJoinResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const Puzzle& pz = *b.pz;

    // ---- T = unhappy cells
    std::vector<int> term;
    for (int cell = 0; cell < pz.ncells; ++cell)
        if (b.cellCostA[cell] > 0) term.push_back(cell);
    if ((int)term.size() > pr.maxCells) {
        std::stable_sort(term.begin(), term.end(),
                         [&](int x, int y) { return b.cellCostA[x] > b.cellCostA[y]; });
        term.resize(pr.maxCells);
        std::sort(term.begin(), term.end());
    }
    if (((int)term.size() & 1) && !term.empty()) term.pop_back();
    res.unhappy = (int)term.size();
    if (res.unhappy < 2) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.ran = true;

    std::vector<std::vector<int>> chains;
    if (!TJoinChains(pz, term, chains, ms)) {
        res.error = "matching solver failed";
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.paths = (int)chains.size();
    res.matchingEdges = 0;
    for (auto& c : chains) res.matchingEdges += (int)c.size() - 1;
    if (chains.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- cyclically shift the pieces along each chain, best rotation for each
    //      landing cell.  Try the chain and its reverse; keep whichever helps.
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long before = b.cost;
    long long bestCost = before;
    std::vector<int> bestPc = b.pc, bestRt = b.rt;
    int applied = 0, moved = 0;

    auto tryShift = [&](const std::vector<int>& chain, bool reverse) -> long long {
        b.load(bestPc, bestRt);
        std::vector<int> np = b.pc, nr = b.rt;
        const int L = (int)chain.size();
        for (int i = 0; i < L; ++i) {
            const int from = chain[reverse ? ((i + 1) % L) : ((i + L - 1) % L)];
            const int to = chain[i];
            // best rotation for this piece in this cell
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
        for (int cell : chain) { b.pc[cell] = np[cell]; b.rt[cell] = nr[cell]; }
        b.rebuild();
        return b.cost;
    };

    std::vector<char> touched(pz.ncells, 0);
    for (const auto& chain : chains) {
        if (applied >= pr.maxPaths) break;
        if ((int)chain.size() > pr.maxPathLen) continue;
        bool clash = false;
        for (int cell : chain) if (touched[cell]) clash = true;
        if (clash) continue;      // another chain already moved one of these
        const long long cFwd = tryShift(chain, false);
        const std::vector<int> pcF = b.pc, rtF = b.rt;
        const long long cRev = tryShift(chain, true);
        if (cFwd <= cRev) { b.load(pcF, rtF); }
        if (b.cost < bestCost) {
            bestCost = b.cost;
            bestPc = b.pc;
            bestRt = b.rt;
            ++applied;
            moved += (int)chain.size();
            for (int cell : chain) touched[cell] = 1;
        } else {
            b.load(bestPc, bestRt);
        }
    }
    b.load(bestPc, bestRt);
    res.shifts = applied;
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

}  // namespace e2
