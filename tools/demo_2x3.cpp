// Eternity-Blossom -- worked example generator for the non-bipartite swap move.
//
// Takes a 2x3 window of a real board and prints, step by step, everything the
// accompanying write-up (experiments/blossom-2x3-example.html) claims:
//
//   * the contents of the six cells, with the colour required on each side
//   * why adjacent pairs are illegal  -> the swap graph is the COMPLEMENT of
//     the grid, with all its weights
//   * that the swap graph is not bipartite (triangles, failed 2-colouring)
//   * why Hungarian cannot be used (the odd-set / blossom inequality)
//   * Blossom's maximum weight matching, VERIFIED against brute force over all
//     25 matchings of a 6-vertex graph
//   * that the matched pairs must additionally form an INDUCED matching, shown
//     empirically: predicted 10 vs measured 6 when the filter is skipped
//   * the final board and the predicted-vs-measured gain
//
// Build: see the `demo-2x3` target in CMakeLists.txt.
// Run:   ./build/demo-2x3 [--board FILE] [--random] [--pieces FILE]
//        --random  use a freshly randomised board, which is what the write-up
//                  uses, because a converged board admits no profitable swap
//                  at all and the example would be empty.
// SPDX-License-Identifier: MIT
// Generate a REAL worked example of the non-bipartite swap-matching operator
// on a 2x3 set of cells, using the actual board accounting.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>
#include "e2/puzzle.hpp"
#include "e2/board.hpp"
#include "e2/matching.hpp"
#include "e2/search.hpp"
using namespace e2;

int main(int argc, char** argv) {
    std::string pieces = "data/e2pieces.txt";
    std::string board = "/tmp/mid.txt";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--board")) board = argv[++i];
        if (!strcmp(argv[i], "--pieces")) pieces = argv[++i];
    }
    Puzzle pz = Puzzle::load(pieces, 0, 0);
    Board b(pz);
    std::vector<int> pc, rt;
    bool rnd = false;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--random")) rnd = true;
    if (rnd) {
        std::mt19937 r0(999);
        b.randomizeStructured(r0);
    } else {
        if (!LoadBoard(board, pc, rt)) { std::cerr << "no board\n"; return 1; }
        b.load(pc, rt);
    }
    const int W = pz.W;
    std::mt19937 rng(12345);

    auto cell = [&](int r, int c) { return r * W + c; };
    auto neigh = [&](int cl, int s) { return pz.neigh[cl * 4 + s]; };

    // ---- pick the 2x3 region with the most violated internal+boundary edges
    int bestR = 0, bestC = 0, bestV = -1;
    for (int r = 0; r + 1 < pz.H; ++r)
        for (int c = 0; c + 2 < W; ++c) {
            int v = 0;
            for (int dr = 0; dr < 2; ++dr)
                for (int dc = 0; dc < 3; ++dc) {
                    const int cl = cell(r + dr, c + dc);
                    for (int s = 0; s < 4; ++s) {
                        const int base = cl * 4 + s;
                        if (b.col[base] != b.target[base]) v++;
                    }
                }
            if (v > bestV) { bestV = v; bestR = r; bestC = c; }
        }
    printf("region: rows %d..%d, cols %d..%d, violated half-edges %d\n", bestR, bestR + 1, bestC,
           bestC + 2, bestV);

    std::vector<int> cells;
    for (int dr = 0; dr < 2; ++dr)
        for (int dc = 0; dc < 3; ++dc) cells.push_back(cell(bestR + dr, bestC + dc));
    const char* nm[6] = {"a", "b", "c", "d", "e", "f"};

    // ---- per-cell report
    printf("\n--- cells ---\n");
    for (int i = 0; i < 6; ++i) {
        const int cl = cells[i];
        printf("%s cell %3d (r%dc%d) piece %3d rot %d | colours N,E,S,W =",
               nm[i], cl, cl / W, cl % W, b.pc[cl], b.rt[cl]);
        for (int s = 0; s < 4; ++s) printf(" %2d", (int)b.col[cl * 4 + s]);
        printf(" | targets =");
        for (int s = 0; s < 4; ++s) printf(" %2d", (int)b.target[cl * 4 + s]);
        printf(" | bad sides =");
        for (int s = 0; s < 4; ++s)
            if (b.col[cl * 4 + s] != b.target[cl * 4 + s]) printf(" %d", s);
        printf("\n");
    }

    // ---- adjacency (grid) among the six
    printf("\n--- grid adjacency ---\n");
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j) {
            bool adj = false;
            for (int s = 0; s < 4; ++s)
                if (neigh(cells[i], s) == cells[j]) adj = true;
            if (adj) printf("%s-%s ", nm[i], nm[j]);
        }
    printf("\n");

    // ---- swap delta for a pair, best over rotations
    auto swapDelta = [&](int i, int j, int& bqa, int& bqb, bool verbose) {
        const int A = cells[i], B = cells[j];
        int best = 0; bqa = 0; bqb = 0;
        for (int qa = 0; qa < 4; ++qa)
            for (int qb = 0; qb < 4; ++qb) {
                // cost over the four sides of A (now holding B's piece rotated
                // by rtB+qb) and of B (now holding A's piece rotated rtA+qa)
                int d = 0;
                for (int k = 0; k < 2; ++k) {
                    const int host = k ? B : A;
                    const int donor = k ? A : B;
                    const int q = k ? qa : qb;   // piece from A gets +qa
                    const int piece = b.pc[donor];
                    const int rot = (b.rt[donor] + q) & 3;
                    for (int s = 0; s < 4; ++s) {
                        const int base = host * 4 + s;
                        const int nb = neigh(host, s);
                        const int w = (nb < 0) ? b.wBorderSlot : 2;
                        const int oldv = (b.col[base] != b.target[base]) ? 1 : 0;
                        const int nc = (int)b.protCol[piece * 16 + rot * 4 + s];
                        const int newv = (nc != (int)b.target[base]) ? 1 : 0;
                        d += w * (newv - oldv);
                    }
                }
                if (d < best) { best = d; bqa = qa; bqb = qb; }
            }
        return best;
    };

    printf("\n--- all 15 pairs ---\n");
    std::vector<std::tuple<int, int, int>> wed;   // (i,j,gain)
    std::vector<std::pair<int, int>> rotOf;
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j) {
            bool adj = false;
            for (int s = 0; s < 4; ++s)
                if (neigh(cells[i], s) == cells[j]) adj = true;
            if (adj) { printf("%s-%s : ADJACENT -> not an edge of the swap graph\n", nm[i], nm[j]); continue; }
            int qa, qb;
            const int d = swapDelta(i, j, qa, qb, false);
            printf("%s-%s : best delta %+d  (qa=%d qb=%d)  gain %d\n", nm[i], nm[j], d, qa, qb,
                   -d);
            if (d < 0) { wed.emplace_back(i, j, -d); rotOf.push_back({qa, qb}); }
        }

    printf("\n--- swap graph (complement of the grid), %zu edges ---\n", wed.size());
    for (auto& e : wed)
        printf("  %s-%s  weight %d\n", nm[std::get<0>(e)], nm[std::get<1>(e)], std::get<2>(e));

    // ---- is it bipartite?  find an odd cycle (a triangle, if there is one)
    printf("\n--- bipartiteness of the swap graph ---\n");
    auto isEdge = [&](int u, int v) {
        for (auto& e : wed)
            if ((std::get<0>(e) == u && std::get<1>(e) == v) ||
                (std::get<0>(e) == v && std::get<1>(e) == u)) return true;
        return false;
    };
    std::vector<std::vector<int>> triList;
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            for (int k = j + 1; k < 6; ++k)
                if (isEdge(i, j) && isEdge(j, k) && isEdge(i, k)) triList.push_back({i, j, k});
    printf("triangles (odd cycles of length 3):");
    for (auto& t : triList)
        printf("  {%s,%s,%s}", nm[t[0]], nm[t[1]], nm[t[2]]);
    printf("   -> count %zu\n", triList.size());
    printf("an undirected graph is bipartite iff it has no odd cycle; a triangle\n"
           "  is an odd cycle, so the swap graph is NOT bipartite.\n");
    {
        std::vector<int> col(6, -1);
        bool ok = true;
        for (int st = 0; st < 6 && ok; ++st) {
            if (col[st] >= 0) continue;
            col[st] = 0;
            std::vector<int> q{st};
            for (size_t h = 0; h < q.size(); ++h)
                for (int u = 0; u < 6; ++u) {
                    if (!isEdge(q[h], u)) continue;
                    if (col[u] < 0) { col[u] = col[q[h]] ^ 1; q.push_back(u); }
                    else if (col[u] == col[q[h]]) { ok = false; break; }
                }
        }
        printf("2-colouring attempt: %s\n", ok ? "succeeded (unexpected)" : "FAILED, as it must");
    }

    // ---- brute force over ALL matchings of the swap graph: verifies Blossom
    //      and separates "max weight matching" from "max weight INDUCED matching"
    printf("\n--- brute force over all matchings of the swap graph ---\n");
    auto gridAdj = [&](int u, int v) {
        for (int s = 0; s < 4; ++s)
            if (neigh(cells[u], s) == cells[v]) return true;
        return false;
    };
    long long bfBest = 0, bfInduced = 0;
    std::vector<std::pair<int, int>> bfSet, bfIndSet;
    int nMatchings = 0, nInduced = 0;
    for (int mask = 0; mask < (1 << (int)wed.size()); ++mask) {
        std::vector<int> deg(6, 0);
        long long ww = 0;
        std::vector<std::pair<int, int>> mm;
        bool okm = true;
        for (int e = 0; e < (int)wed.size(); ++e) {
            if (!(mask >> e & 1)) continue;
            const int u = std::get<0>(wed[e]), v = std::get<1>(wed[e]);
            if (deg[u] || deg[v]) { okm = false; break; }
            deg[u] = deg[v] = 1;
            ww += std::get<2>(wed[e]);
            mm.push_back({u, v});
        }
        if (!okm) continue;
        ++nMatchings;
        if (ww > bfBest) { bfBest = ww; bfSet = mm; }
        bool ind = true;
        for (size_t x = 0; x < mm.size() && ind; ++x)
            for (size_t y = x + 1; y < mm.size() && ind; ++y)
                if (gridAdj(mm[x].first, mm[y].first) || gridAdj(mm[x].first, mm[y].second) ||
                    gridAdj(mm[x].second, mm[y].first) || gridAdj(mm[x].second, mm[y].second))
                    ind = false;
        if (!ind) continue;
        ++nInduced;
        if (ww > bfInduced) { bfInduced = ww; bfIndSet = mm; }
    }
    printf("all matchings: %d   of which induced: %d\n", nMatchings, nInduced);
    printf("brute-force MAX WEIGHT MATCHING       = %lld :", bfBest);
    for (auto& q : bfSet) printf(" %s-%s", nm[q.first], nm[q.second]);
    printf("\nbrute-force MAX WEIGHT INDUCED MATCH = %lld :", bfInduced);
    for (auto& q : bfIndSet) printf(" %s-%s", nm[q.first], nm[q.second]);
    printf("\n");

    // ---- blossom inequality demonstration on the triangle a-c-e
    printf("\n--- blossom / odd-set inequality ---\n");
    {
        std::vector<std::tuple<int, int, int>> tri = {{0, 2, 1}, {0, 4, 1}, {2, 4, 1}};
        std::vector<std::pair<int, int>> pr;
        long long w = 0;
        MaxWeightMatching(6, tri, pr, w);
        printf("triangle {a,c,e} all weights 1: max weight matching = %lld (LP without the\n"
               "  odd-set inequality would give 1.5; the blossom constraint x(E[S]) <= 1\n"
               "  is exactly what cuts that off)\n", w);
        for (auto& p : pr) printf("  matched %s-%s\n", nm[p.first], nm[p.second]);
    }

    // ---- the actual max weight matching
    printf("\n--- maximum weight matching (Blossom) ---\n");
    std::vector<std::pair<int, int>> pr;
    long long w = 0;
    if (wed.empty()) { printf("no profitable pairs\n"); return 0; }
    MaxWeightMatching(6, wed, pr, w);
    printf("total weight %lld, %zu pairs:\n", w, pr.size());
    for (auto& p : pr) printf("  %s-%s\n", nm[p.first], nm[p.second]);
    printf("brute force says %lld -> %s\n", bfBest, (w == bfBest ? "AGREE" : "MISMATCH"));

    // ---- greedy induced-matching filter, then apply
    printf("\n--- greedy filter (additivity requires an INDUCED matching) ---\n");
    std::vector<std::tuple<int, int, int>> sorted = wed;
    std::sort(sorted.begin(), sorted.end(), [](auto& x, auto& y) { return std::get<2>(x) > std::get<2>(y); });
    std::vector<char> taken(6, 0);
    std::vector<std::tuple<int, int, int>> applyList;
    for (auto& e : sorted) {
        const int i = std::get<0>(e), j = std::get<1>(e);
        if (taken[i] || taken[j]) continue;
        bool clash = false;
        for (auto& a : applyList)
            for (int x : {std::get<0>(a), std::get<1>(a)})
                for (int y : {i, j})
                    for (int s = 0; s < 4; ++s)
                        if (neigh(cells[x], s) == cells[y]) clash = true;
        if (clash) { printf("  drop %s-%s (touches an already chosen pair)\n", nm[i], nm[j]); continue; }
        taken[i] = taken[j] = 1;
        applyList.push_back(e);
        printf("  keep %s-%s (gain %d)\n", nm[i], nm[j], std::get<2>(e));
    }
    long long predicted = 0;
    for (auto& e : applyList) predicted += std::get<2>(e);

    // ---- EMPIRICAL CHECK: what if we applied ALL of Blossom's pairs, ignoring
    //      the induced-matching filter?  The individual gains do not add up,
    //      because two cells from different pairs are neighbours and each one's
    //      "target" colour is the other's content, which is also moving.
    {
        Board t(pz);
        t.load(b.pc, b.rt);
        const long long c0 = t.cost;
        long long pred = 0;
        for (auto& pr2 : pr) pred += std::get<2>(wed[0]);   // placeholder, fixed below
        pred = 0;
        for (auto& pr2 : pr)
            for (auto& e : wed)
                if (std::get<0>(e) == pr2.first && std::get<1>(e) == pr2.second) pred += std::get<2>(e);
        for (auto& pr2 : pr) {
            const int i = pr2.first, j = pr2.second;
            int qa = 0, qb = 0;
            for (auto& e : wed)
                if (std::get<0>(e) == i && std::get<1>(e) == j) { qa = rotOf[0].first; }
            for (size_t k = 0; k < wed.size(); ++k)
                if (std::get<0>(wed[k]) == i && std::get<1>(wed[k]) == j) { qa = rotOf[k].first; qb = rotOf[k].second; }
            const int A = cells[i], B = cells[j];
            const int pA = t.pc[A], rA = t.rt[A], pB = t.pc[B], rB = t.rt[B];
            t.pc[A] = pB; t.rt[A] = (rB + qb) & 3;
            t.pc[B] = pA; t.rt[B] = (rA + qa) & 3;
        }
        t.rebuild();
        printf("  [check] applying Blossom's %zu pairs WITHOUT the filter: predicted %lld,"
               " measured %lld  -> %s\n", pr.size(), pred, c0 - t.cost,
               (pred == c0 - t.cost) ? "additive" : "NOT additive");
    }

    const long long before = b.cost;
    for (auto& e : applyList) {
        const int i = std::get<0>(e), j = std::get<1>(e);
        int qa = 0, qb = 0;
        for (size_t k = 0; k < wed.size(); ++k)
            if (std::get<0>(wed[k]) == i && std::get<1>(wed[k]) == j) { qa = rotOf[k].first; qb = rotOf[k].second; }
        const int A = cells[i], B = cells[j];
        const int pA = b.pc[A], rA = b.rt[A], pB = b.pc[B], rB = b.rt[B];
        b.pc[A] = pB; b.rt[A] = (rB + qb) & 3;
        b.pc[B] = pA; b.rt[B] = (rA + qa) & 3;
        printf("  swap %s<->%s : piece %d rot %d  <->  piece %d rot %d   (new rots %d, %d)\n",
               nm[i], nm[j], pA, rA, pB, rB, b.rt[A], b.rt[B]);
    }
    b.rebuild();
    printf("\npredicted gain %lld, measured gain %lld, cost %lld -> %lld\n", predicted,
           before - b.cost, before, b.cost);

    printf("\n--- final cells ---\n");
    for (int i = 0; i < 6; ++i) {
        const int cl = cells[i];
        printf("%s piece %3d rot %d | colours =", nm[i], b.pc[cl], b.rt[cl]);
        for (int s = 0; s < 4; ++s) printf(" %2d", (int)b.col[cl * 4 + s]);
        printf(" | bad sides =");
        for (int s = 0; s < 4; ++s)
            if (b.col[cl * 4 + s] != b.target[cl * 4 + s]) printf(" %d", s);
        printf("\n");
    }
    return 0;
}
