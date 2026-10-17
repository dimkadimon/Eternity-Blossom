// Eternity-Blossom -- matching engine.
//
// SPDX-License-Identifier: MIT
//
// See matching.hpp for the rationale.  The only file in this repository that
// includes GPL-3.0 licensed code is this one, and only when the build is
// configured with -DE2_USE_BLOSSOM_VI=ON (the default when the dependency can
// be fetched).  Blossom VI itself is NOT redistributed here: CMake fetches it
// from its upstream repository at configure time.

#include "e2/matching.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#ifdef E2_HAVE_BLOSSOM_VI
#include "MWPMSolver.h"
#endif

namespace e2 {

namespace {

constexpr long long kInf = (1LL << 50);

// ---------------------------------------------------------------------------
// MIT-only fallback (no Blossom VI): exact subset DP for small n, greedy+2-opt
// for larger n.  Only used when the project is built without the dependency.
// ---------------------------------------------------------------------------
bool FallbackMinWeightPerfectMatching(int n, const std::vector<WEdge>& edges,
                                      std::vector<int>& mate, long long& weight) {
    if (n % 2 != 0) return false;
    std::vector<std::vector<std::pair<int, int>>> adj(n);
    for (const auto& [u, v, w] : edges) {
        if (u < 0 || u >= n || v < 0 || v >= n || u == v) continue;
        adj[u].emplace_back(v, w);
        adj[v].emplace_back(u, w);
    }
    for (int v = 0; v < n; ++v) if (adj[v].empty()) return false;  // no perfect matching

    if (n <= 18) {
        const int N = 1 << n;
        std::vector<long long> dp(N, kInf);
        // par[mask] = the pair that was added last on the cheapest way to build
        // `mask`.  Needed because the reconstruction cannot re-derive the path
        // from the dp values alone: the DP always extends by the lowest-numbered
        // uncovered vertex, so it never evaluates dp[S] for sets S that exclude
        // vertex 0, and the "is this pair on an optimal path?" test would need
        // exactly those states.
        std::vector<std::pair<int, int>> par(N, {-1, -1});
        dp[0] = 0;
        par[0] = {-2, -2};
        for (int mask = 0; mask < N; ++mask) {
            if (dp[mask] >= kInf) continue;
            int v = 0;
            while (v < n && ((mask >> v) & 1)) ++v;
            if (v == n) continue;
            for (auto [u, w] : adj[v]) {
                if ((mask >> u) & 1) continue;
                const int nm = mask | (1 << v) | (1 << u);
                const long long cand = dp[mask] + w;
                if (cand < dp[nm]) {
                    dp[nm] = cand;
                    par[nm] = {v, u};
                }
            }
        }
        const int full = N - 1;
        if (dp[full] >= kInf) return false;
        weight = dp[full];
        mate.assign(n, -1);
        // Walk the recorded parents back from "everything matched" to "nothing
        // matched", emitting one matched pair per step.
        int mask = full;
        while (mask) {
            const auto [v, u] = par[mask];
            if (v < 0) return false;
            mate[v] = u;
            mate[u] = v;
            mask &= ~(1 << v);
            mask &= ~(1 << u);
        }
        return true;
    }
    // Greedy + local search fallback for larger graphs (approximate).
    std::vector<WEdge> es = edges;
    std::sort(es.begin(), es.end(), [](const WEdge& a, const WEdge& b) {
        return std::get<2>(a) < std::get<2>(b);
    });
    mate.assign(n, -1);
    for (auto [u, v, w] : es) {
        if (mate[u] < 0 && mate[v] < 0) { mate[u] = v; mate[v] = u; }
    }
    for (int v = 0; v < n; ++v) {
        if (mate[v] >= 0) continue;
        // forced repair: match with the cheapest available partner
        for (auto [u, w] : adj[v]) {
            if (mate[u] < 0) { mate[u] = v; mate[v] = u; break; }
        }
    }
    // 2-opt on matched pairs
    bool improved = true;
    int rounds = 0;
    while (improved && rounds++ < 60) {
        improved = false;
        for (auto [a, b, wab] : es) {
            if (mate[a] != b) continue;
            for (auto [c, d, wcd] : es) {
                if (mate[c] != d) continue;
                if (a == c || a == d || b == c || b == d) continue;
                const int w1 = wab + wcd;
                int w2 = 0;
                bool ok = false;
                for (auto [x, w] : adj[a]) if (x == c) { w2 += w; ok = true; }
                if (!ok) continue;
                ok = false;
                for (auto [x, w] : adj[b]) if (x == d) { w2 += w; ok = true; }
                if (!ok) continue;
                if (w2 < w1) {
                    mate[a] = c; mate[c] = a; mate[b] = d; mate[d] = b;
                    improved = true;
                    break;
                }
            }
            if (improved) break;
        }
    }
    weight = 0;
    for (int v = 0; v < n; ++v) {
        if (mate[v] < 0) return false;
        if (v < mate[v]) {
            for (auto [u, w] : adj[v]) if (u == mate[v]) weight += w;
        }
    }
    return true;
}

}  // namespace

bool MatchingEngineIsExact() {
#if E2_HAVE_BLOSSOM_VI
    return true;
#else
    return false;   // exact only for n <= 18; heuristic above that
#endif
}

bool HaveBlossomVI() {
#ifdef E2_HAVE_BLOSSOM_VI
    return true;
#else
    return false;
#endif
}

bool MinWeightPerfectMatchingCertified(int n, const std::vector<WEdge>& edges,
                                       std::vector<int>& mate, long long& weight,
                                       MatchingCertificate& cert,
                                       MatchingStats* stats) {
    cert = MatchingCertificate{};
#ifdef E2_HAVE_BLOSSOM_VI
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = false;
    try {
        SolverParameters params;
        params.compute_dual_certificate = true;
        MWPMSolver solver(edges, params);
        solver.FindMinPerfectMatching();
        mate.assign(n, -1);
        for (const auto& [u, v] : solver.Matching()) { mate[u] = v; mate[v] = u; }
        weight = solver.primal_objective;
        cert.primal = solver.primal_objective;
        cert.dual = solver.dual_objective;
        cert.blossoms = solver.DualCertificate();

        // Blossom VI reports every live node, singletons included, in a forest
        // given by parent pointers.  A strictly positive dual marks a genuine
        // blossom (an odd set that had to be contracted) rather than a plain
        // vertex; the depth of the forest says how deeply blossoms nested.
        cert.blossomCount = (int)cert.blossoms.size();
        std::vector<int> parent(cert.blossomCount, -1);
        for (size_t i = 0; i < cert.blossoms.size(); ++i) {
            const auto& [idx, dual4, par] = cert.blossoms[i];
            (void)idx;
            if (dual4 > 0) ++cert.oddBlossoms;
            if (par >= 0 && par < cert.blossomCount) parent[i] = par;
        }
        std::vector<int> depth(cert.blossomCount, 0);
        for (int i = 0; i < cert.blossomCount; ++i) {
            int d = 0, v = i, guard = 0;
            while (parent[v] >= 0 && guard++ <= cert.blossomCount) { ++d; v = parent[v]; }
            depth[i] = d;
            cert.maxDepth = std::max(cert.maxDepth, d);
        }
        // primal == dual is exactly the certificate of optimality
        cert.valid = (cert.primal == cert.dual);
        ok = true;
    } catch (...) {
        ok = false;
    }
    if (stats) {
        stats->calls += ok ? 1 : 0;
        stats->totalVertices += n;
        stats->totalEdges += (long long)edges.size();
        stats->seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    if (ok) return true;
#endif
    // No Blossom VI (or the solve failed): an ordinary solve still gives the
    // matching, just not a certificate.  Never claim one we do not have.
    (void)stats;
    return MinWeightPerfectMatching(n, edges, mate, weight, stats);
}

std::string MatchingEngineName() {
#ifdef E2_HAVE_BLOSSOM_VI
    return "Blossom-VI (Arkhipov & Kolmogorov, arXiv:2604.20351)";
#else
    return "built-in fallback (subset-DP / greedy-2opt) -- build with -DE2_USE_BLOSSOM_VI=ON";
#endif
}

bool MinWeightPerfectMatching(int n, const std::vector<WEdge>& edges,
                              std::vector<int>& mate, long long& weight,
                              MatchingStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = false;
#ifdef E2_HAVE_BLOSSOM_VI
    try {
        SolverParameters params;
        params.compute_dual_certificate = false;
        params.verbose = false;
        MWPMSolver solver(edges, params);
        solver.FindMinPerfectMatching();
        mate.assign(n, -1);
        for (const auto& [u, v] : solver.Matching()) {
            mate[u] = v;
            mate[v] = u;
        }
        weight = solver.primal_objective;
        ok = true;
    } catch (...) {
        ok = false;  // most likely: the graph has no perfect matching
    }
    if (!ok) {
        // Distress signal: Blossom VI also fails on graphs with isolated
        // vertices, which our callers must not produce.  Fall back so that a
        // caller-side bug degrades into a wrong-but-loud result rather than a
        // crash -- the fallback returns false for isolated vertices too.
        ok = FallbackMinWeightPerfectMatching(n, edges, mate, weight);
        if (stats) stats->fallbackCalls += ok ? 1 : 0;
        if (stats && ok) stats->fallbackCalls += 0;
    }
#else
    ok = FallbackMinWeightPerfectMatching(n, edges, mate, weight);
    if (stats) ++stats->fallbackCalls;
#endif
    if (stats) {
        ++stats->calls;
        stats->totalVertices += n;
        stats->totalEdges += (long long)edges.size();
        const auto t1 = std::chrono::steady_clock::now();
        stats->seconds += std::chrono::duration<double>(t1 - t0).count();
    }
    return ok;
}

bool MaxWeightMatching(int n, const std::vector<WEdge>& edges,
                       std::vector<std::pair<int, int>>& pairs, long long& weight,
                       MatchingStats* stats) {
    pairs.clear();
    weight = 0;
    if (n <= 1) return true;
    // Dummy-clique reduction:
    //   vertices 0..n-1        : the real vertices
    //   vertices n..2n-1       : dummy i, a "free" partner for real vertex i
    //   edges (i, j)           : weight -w(i,j)      (we minimise)
    //   edges (i, n+i)         : weight 0            (i stays unmatched)
    //   edges (n+i, n+j)       : weight 0            (dummies pair up among
    //                                                themselves)
    // Any perfect matching of the 2n vertices induces a matching M on the real
    // vertices with cost -sum_{e in M} w(e), and every real matching M can be
    // completed to a perfect one: the 2m vertices left over are matched to
    // dummies, and the remaining n-2m dummies (an even number) pair up inside
    // the dummy clique.  Minimising therefore maximises sum w over M.
    //
    // Only strictly positive weights are passed in: a non-positive edge never
    // helps (leaving both endpoints unmatched is free), and dropping them keeps
    // the instance small.
    std::vector<WEdge> big;
    big.reserve(edges.size() + n + (size_t)n * (n - 1) / 2);
    for (const auto& [u, v, w] : edges) {
        if (u == v || u < 0 || v < 0 || u >= n || v >= n) continue;
        if (w <= 0) continue;
        big.emplace_back(u, v, -w);
    }
    for (int i = 0; i < n; ++i) big.emplace_back(i, n + i, 0);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) big.emplace_back(n + i, n + j, 0);

    std::vector<int> mate;
    long long w = 0;
    if (!MinWeightPerfectMatching(2 * n, big, mate, w, stats)) return false;
    weight = -w;
    for (int i = 0; i < n; ++i) {
        const int j = mate[i];
        if (j >= 0 && j < n && i < j) pairs.emplace_back(i, j);
    }
    return true;
}

}  // namespace e2
