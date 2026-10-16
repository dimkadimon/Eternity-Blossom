// Eternity-Blossom -- command line entry point.
//
// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "e2/matching.hpp"
#include "e2/operators.hpp"
#include "e2/puzzle.hpp"
#include "e2/search.hpp"

using namespace e2;

namespace {

void PrintUsage(const char* prog) {
    std::cerr
        << "Eternity-Blossom -- Eternity II with Blossom VI perfect matching\n\n"
        << "usage: " << prog << " <command> [options]\n\n"
        << "commands:\n"
        << "  solve    search for a high-scoring board\n"
        << "  verify   re-score a saved board from scratch\n"
        << "  bound    compute the stub-matching lower bound at the root\n"
        << "  selftest correctness checks (matching, increments, bound)\n\n"
        << "options for solve:\n"
        << "  --pieces FILE     piece file (default data/e2pieces.txt)\n"
        << "  --w N --h N       board size (default: as square as the piece count allows)\n"
        << "  --time SEC        wall clock limit (default 60)\n"
        << "  --threads N       independent searches (default 1)\n"
        << "  --seed N          base RNG seed (default 1)\n"
        << "  --out FILE        where to write the best board\n"
        << "  --init MODE       greedy|structured|random|greedystruct (default greedystruct)\n"
        << "  --lahc N          late-acceptance history length (default 500)\n"
        << "  --targeted F      fraction of aimed moves: violated edge + matching colour\n"
        << "  --cycle3 F        fraction of aimed moves that are 3-cycles instead of swaps\n"
        << "  --stall N         iterations without improvement before perturbing (default 300000)\n"
        << "  --perturb N       random swaps used as a perturbation (default 24)\n"
        << "  --assign N        cells per optimal-reassignment pass (default 64, 0=off)\n"
        << "  --assign-every N  iterations between reassignment passes (default 200000)\n"
        << "  --accept lahc|sa  acceptance criterion (default lahc)\n"
        << "  --t0 F --t1 F     SA temperatures, start and end (default 4.0 0.15)\n"
        << "  --sample K        candidate proposals evaluated per move (default 1)\n"
        << "  --wborder N       cost of a non-grey border edge (default 8)\n"
        << "  --noblossom       disable the Blossom swap-packet operator\n"
        << "  --blossom-every N consider a blossom pass every N iterations (default 20000)\n"
        << "  --blossom-stall N trigger a blossom pass after N stalled iterations (default 4000)\n"
        << "  --blossom-cells N cap on candidate cells per pass (default 96)\n"
        << "  --region          enable exact region re-optimisation (branch & bound + bound)\n"
        << "  --region-size N   cells per region (default 8)\n"
        << "  --json            emit a machine readable summary\n";
}

struct Args {
    std::unordered_map<std::string, std::string> opt;
    std::string get(const std::string& k, const std::string& def) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : it->second;
    }
    int getInt(const std::string& k, int def) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : std::atoi(it->second.c_str());
    }
    double getDouble(const std::string& k, double def) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : std::atof(it->second.c_str());
    }
};

Args ParseArgs(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s;
            std::string val = "1";
            const size_t eq = s.find('=');
            if (eq != std::string::npos) { key = s.substr(0, eq); val = s.substr(eq + 1); }
            if (i + 1 < argc && argv[i + 1][0] != '-') { val = argv[++i]; }
            a.opt[key] = val;
        }
    }
    return a;
}

// --------------------------------------------------------------------------
int CmdSolve(const Args& a) {
    const std::string piecePath = a.get("--pieces", "data/e2pieces.txt");
    Puzzle pz;
    try {
        pz = Puzzle::load(piecePath, a.getInt("--w", 0), a.getInt("--h", 0));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cout << pz.verify();
    std::cout << "matching engine: " << MatchingEngineName() << "\n";

    SearchParams sp;
    sp.seed = (uint64_t)a.getInt("--seed", 1);
    sp.timeLimitSeconds = a.getDouble("--time", 60.0);
    sp.lahcLength = a.getInt("--lahc", 500);
    sp.wBorder = a.getInt("--wborder", 8);
    sp.useBlossomSwaps = !a.opt.count("--noblossom");
    sp.blossomCheckEvery = a.getInt("--blossom-every", 20000);
    sp.blossomStallTrigger = a.getInt("--blossom-stall", 4000);
    sp.blossom.maxCells = a.getInt("--blossom-cells", 96);
    sp.pTargeted = a.getDouble("--targeted", 0.85);
    sp.trace = a.opt.count("--trace") > 0;
    if (a.get("--accept", "lahc") == "sa") sp.accept = AcceptMode::SA;
    sp.saT0 = a.getDouble("--t0", 4.0);
    sp.saT1 = a.getDouble("--t1", 0.15);
    sp.sampleK = a.getInt("--sample", 1);
    sp.pCycle3 = a.getDouble("--cycle3", 0.35);
    sp.useAssign = !a.opt.count("--noassign");
    sp.assignCells = a.getInt("--assign", 64);
    sp.assignEvery = a.getInt("--assign-every", 200000);
    sp.stallLimit = a.getInt("--stall", 300000);
    sp.perturbSwaps = a.getInt("--perturb", 24);
    sp.useRegionLNS = a.opt.count("--region") > 0;
    sp.regionSize = a.getInt("--region-size", 8);
    const std::string init = a.get("--init", "greedystruct");
    if (init == "greedy") sp.init = InitMode::Greedy;
    else if (init == "structured") sp.init = InitMode::StructuredRandom;
    else if (init == "random") sp.init = InitMode::Random;
    else sp.init = InitMode::GreedyStructured;

    const int threads = a.getInt("--threads", 1);
    const auto t0 = std::chrono::steady_clock::now();
    SearchOutcome out = SearchParallel(pz, sp, threads);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    BoardCheck ck = CheckBoard(pz, out.pc, out.rt);
    std::cout << "\n=== result ===\n";
    std::cout << "matched edges : " << ck.matched << " / " << pz.numEdges() << "\n";
    std::cout << "mismatched    : " << ck.mismatched << "\n";
    std::cout << "border bad    : " << ck.borderBad << "\n";
    std::cout << "uses every piece exactly once: " << (ck.valid ? "yes" : "NO") << "\n";
    std::cout << "wall seconds  : " << std::fixed << std::setprecision(2) << wall << "\n";
    std::cout << "iterations    : " << out.stats.iterations << "\n";
    std::cout << "perturbations : " << out.stats.restarts << "\n";
    std::cout << "blossom passes: " << out.stats.blossomPasses
              << " (improved " << out.stats.blossomImproved
              << ", reverted " << out.stats.blossomReverted << ")\n";
    std::cout << "blossom gain  : predicted " << out.stats.blossomTotalPredicted
              << ", measured " << out.stats.blossomTotalActual << "\n";
    std::cout << "assign passes : " << out.stats.assignPasses
              << " (improved " << out.stats.assignImproved
              << ")  measured gain " << out.stats.assignTotalActual << "\n";
    std::cout << "matching calls: " << out.stats.matching.calls
              << "  avg vertices " << (out.stats.matching.calls ? out.stats.matching.totalVertices / out.stats.matching.calls : 0)
              << "  avg edges " << (out.stats.matching.calls ? out.stats.matching.totalEdges / out.stats.matching.calls : 0)
              << "  total " << std::setprecision(3) << out.stats.matching.seconds << "s\n";
    if (sp.useRegionLNS) {
        std::cout << "region runs   : " << out.stats.regionRuns
                  << " (improved " << out.stats.regionImproved << ")"
                  << "  nodes " << out.stats.regionNodes
                  << "  bound calls " << out.stats.regionBoundCalls
                  << "  bound prunes " << out.stats.regionBoundPrunes << "\n";
    }

    const std::string outPath = a.get("--out", "results/best_board.txt");
    if (!outPath.empty()) {
        SaveBoard(outPath, pz, out.pc, out.rt, "seed=" + std::to_string(sp.seed));
        std::cout << "board written : " << outPath << "\n";
    }
    if (a.opt.count("--json")) {
        std::cout << "\n{\"matched\":" << ck.matched << ",\"max\":" << pz.numEdges()
                  << ",\"border_bad\":" << ck.borderBad
                  << ",\"seconds\":" << wall
                  << ",\"iterations\":" << out.stats.iterations
                  << ",\"blossom_passes\":" << out.stats.blossomPasses
                  << ",\"blossom_gain\":" << out.stats.blossomTotalActual << "}\n";
    }
    return 0;
}

// --------------------------------------------------------------------------
int CmdVerify(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"), a.getInt("--w", 0), a.getInt("--h", 0));
    std::vector<int> pc, rt;
    std::string comment;
    if (!LoadBoard(a.get("--board", "results/best_board.txt"), pc, rt, &comment)) {
        std::cerr << "error: cannot read board\n";
        return 1;
    }
    if (!comment.empty()) std::cout << comment;
    BoardCheck ck = CheckBoard(pz, pc, rt);
    std::cout << "matched " << ck.matched << "/" << pz.numEdges()
              << "  mismatched " << ck.mismatched
              << "  border_bad " << ck.borderBad
              << "  every-piece-once " << (ck.valid ? "yes" : "NO") << "\n";
    if (ck.matched == pz.numEdges() && ck.borderBad == 0 && ck.valid) {
        std::cout << "*** SOLVED ***\n";
    }
    return 0;
}

// --------------------------------------------------------------------------
int CmdBound(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"), a.getInt("--w", 0), a.getInt("--h", 0));
    std::cout << pz.verify();
    // Whole board, nothing placed: every slot of every cell is "free" except the
    // border slots, which demand grey.  This is the weakest form of the bound --
    // it asks only whether the multiset of piece edges can be paired up at all.
    std::vector<int> cells(pz.ncells), pool(pz.ncells);
    std::iota(cells.begin(), cells.end(), 0);
    std::iota(pool.begin(), pool.end(), 0);
    std::vector<int> req(pz.ncells * 4, -1);
    for (int cell = 0; cell < pz.ncells; ++cell) {
        for (int s = 0; s < 4; ++s) {
            if (pz.neigh[cell * 4 + s] < 0) req[cell * 4 + s] = kGray;
        }
    }
    MatchingStats ms;
    const auto t0 = std::chrono::steady_clock::now();
    StubBoundResult br = StubMatchingBound(pz, cells, pool, req, &ms);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "\nroot stub-matching bound\n";
    std::cout << "  vertices " << br.vertices << "  edges " << br.edges << "\n";
    std::cout << "  lower bound on mismatches: " << (br.infeasible ? std::string("INFEASIBLE") : std::to_string(br.bound)) << "\n";
    std::cout << "  matching time " << std::fixed << std::setprecision(3) << br.seconds
              << "s (total " << secs << "s)\n";
    std::cout << "  engine: " << MatchingEngineName() << "\n";
    std::cout << "\nInterpretation: 0 means this (very weak) relaxation cannot rule\n"
              << "anything out at the root -- expected.  The bound becomes useful\n"
              << "once most of the board is fixed and only a small region is open.\n";
    return 0;
}

// --------------------------------------------------------------------------
// Correctness checks.  These are cheap and are the reason the incremental
// scoring can be trusted.
bool SelfTestMatching(int trials, int maxN, uint64_t seed) {
    std::mt19937_64 rng(seed);
    for (int t = 0; t < trials; ++t) {
        int n = 2 * (2 + (int)(rng() % (maxN / 2)));
        std::vector<WEdge> edges;
        // Random graph with negative/positive weights; ensure a perfect matching
        // exists by planting one.
        std::vector<int> perm(n);
        std::iota(perm.begin(), perm.end(), 0);
        std::shuffle(perm.begin(), perm.end(), rng);
        std::vector<std::pair<int, int>> planted;
        for (int i = 0; i < n; i += 2) planted.emplace_back(perm[i], perm[i + 1]);
        std::vector<char> plantedPair(n * n, 0);
        for (auto [u, v] : planted) {
            edges.emplace_back(u, v, (int)(rng() % 21) - 10);
            plantedPair[u * n + v] = plantedPair[v * n + u] = 1;
        }
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                // Keep the graph simple: parallel edges would make the
                // "look up the weight of (i,j)" step of the checker ambiguous.
                if (plantedPair[i * n + j]) continue;
                if (rng() % 3 == 0) edges.emplace_back(i, j, (int)(rng() % 21) - 10);
            }
        }
        std::vector<int> mate;
        long long w = 0;
        if (!MinWeightPerfectMatching(n, edges, mate, w)) {
            std::cerr << "selftest: solver reported failure on a solvable graph\n";
            return false;
        }
        // Check it is a perfect matching.
        for (int v = 0; v < n; ++v) {
            if (mate[v] < 0 || mate[mate[v]] != v) {
                std::cerr << "selftest: not a perfect matching\n";
                return false;
            }
        }
        // Check the weight.
        long long chk = 0;
        for (int v = 0; v < n; ++v) {
            if (v >= mate[v]) continue;
            bool found = false;
            for (auto [x, y, ww] : edges) {
                if ((x == v && y == mate[v]) || (y == v && x == mate[v])) { chk += ww; found = true; break; }
            }
            if (!found) { std::cerr << "selftest: matched edge not in graph\n"; return false; }
        }
        if (chk != w) { std::cerr << "selftest: weight mismatch " << chk << " vs " << w << "\n"; return false; }
        // Brute-force optimum by subset DP (n is small).
        const int N = 1 << n;
        std::vector<long long> dp(N, (1LL << 50));
        dp[0] = 0;
        std::vector<std::vector<std::pair<int, int>>> adj(n);
        for (auto [u, v, ww] : edges) { adj[u].emplace_back(v, ww); adj[v].emplace_back(u, ww); }
        for (int mask = 0; mask < N; ++mask) {
            if (dp[mask] >= (1LL << 50)) continue;
            int v = 0;
            while (v < n && (mask >> v) & 1) ++v;
            if (v == n) continue;
            for (auto [u, ww] : adj[v]) {
                if ((mask >> u) & 1) continue;
                const int nm = mask | (1 << v) | (1 << u);
                dp[nm] = std::min(dp[nm], dp[mask] + ww);
            }
        }
        const long long opt = dp[N - 1];
        if (opt != w) {
            std::cerr << "selftest: suboptimal matching " << w << " vs brute force " << opt << "\n";
            return false;
        }
    }
    return true;
}

bool SelfTestIncrements(const Puzzle& pz) {
    std::mt19937_64 rng(12345);
    Board b(pz);
    b.randomize(rng);
    for (int iter = 0; iter < 20000; ++iter) {
        const int a = (int)(rng() % pz.ncells);
        int c = (int)(rng() % pz.ncells);
        if (a == c) continue;
        const int ra = (int)(rng() % 4), rc = (int)(rng() % 4);
        if (iter % 7 == 0) {
            // cross-check the fast (single-cell, no neighbour chasing) delta
            // against the exact general delta
            int fra = 0, frb = 0;
            const int fast = b.fastSwapDelta(a, c, &fra, &frb);
            const int gen = b.generalSwapDelta(a, c, fra, frb);
            if (!b.adjacent(a, c) && fast != gen) {
                std::cerr << "selftest: fast delta " << fast << " != exact delta " << gen << "\n";
                return false;
            }
        }
        const int predicted = b.deltaSwapExact(a, c, ra, rc);
        const long long before = b.cost;
        b.applySwap(a, c, ra, rc);
        const long long after = b.cost;
        if (before + predicted != after) {
            std::cerr << "selftest: swap delta wrong: predicted " << predicted
                      << " actual " << (after - before) << "\n";
            return false;
        }
        if (iter % 500 == 0) {
            Board fresh(pz);
            fresh.load(b.pc, b.rt);
            if (fresh.cost != b.cost) {
                std::cerr << "selftest: incremental cost drifted from rebuild\n";
                return false;
            }
        }
        if (rng() % 50 == 0) {
            const int cell = (int)(rng() % pz.ncells);
            const int r = (int)(rng() % 4);
            const int pr = b.deltaRotate(cell, r);
            const long long b0 = b.cost;
            b.applyRotation(cell, r);
            if (b0 + pr != b.cost) {
                std::cerr << "selftest: rotation delta wrong\n";
                return false;
            }
        }
    }
    return true;
}

// The stub bound must never overestimate the true optimum.  We check that on
// tiny random instances where the optimum can be found by brute force.
bool SelfTestBound(int trials, uint64_t seed) {
    std::mt19937_64 rng(seed);
    for (int t = 0; t < trials; ++t) {
        const int W = 2, H = 2;  // 2x2 board, 4 pieces
        const int n = 4;
        Puzzle pz;
        pz.pieces.resize(n);
        // Build a random *solvable* 2x2 instance: lay down a solution first.
        std::vector<int> solCols(n * 4, -1);
        for (int e = 0; e < 4; ++e) {
            // shared edges of the 2x2 board: (0,1) and (2,3) horizontal? build directly
            (void)e;
        }
        // adjacency of a 2x2: cells 0 1 / 2 3. edges: 0-1 (side1/3), 0-2 (side2/0), 1-3, 2-3
        // border: 0:N,W  1:N,E  2:S,W  3:S,E
        auto rnd = [&](int lo, int hi) { return lo + (int)(rng() % (hi - lo + 1)); };
        // choose internal colours and border = 0 (grey)
        std::vector<int> cellCol(n * 4, 0);
        cellCol[0 * 4 + 0] = 0; cellCol[0 * 4 + 3] = 0;                       // cell0 N,W border
        cellCol[1 * 4 + 0] = 0; cellCol[1 * 4 + 1] = 0;                       // cell1 N,E border
        cellCol[2 * 4 + 2] = 0; cellCol[2 * 4 + 3] = 0;                       // cell2 S,W border
        cellCol[3 * 4 + 1] = 0; cellCol[3 * 4 + 2] = 0;                       // cell3 S,E border
        const int c01 = rnd(1, 3), c02 = rnd(1, 3), c13 = rnd(1, 3), c23 = rnd(1, 3);
        cellCol[0 * 4 + 1] = c01; cellCol[1 * 4 + 3] = c01;
        cellCol[0 * 4 + 2] = c02; cellCol[2 * 4 + 0] = c02;
        cellCol[1 * 4 + 2] = c13; cellCol[3 * 4 + 0] = c13;
        cellCol[2 * 4 + 1] = c23; cellCol[3 * 4 + 3] = c23;
        for (int i = 0; i < n; ++i) {
            for (int s = 0; s < 4; ++s) pz.pieces[i].c[s] = cellCol[i * 4 + s];
            pz.pieces[i].period = piecePeriod(pz.pieces[i].c);
        }
        pz.initGeometry(W, H);
        pz.classify();
        // Brute force the optimum over all assignments (4! * 4^4 = 24*256).
        std::vector<int> perm = {0, 1, 2, 3};
        int bestCost = INT32_MAX;
        do {
            for (int r0 = 0; r0 < 4; ++r0) for (int r1 = 0; r1 < 4; ++r1)
            for (int r2 = 0; r2 < 4; ++r2) for (int r3 = 0; r3 < 4; ++r3) {
                const int rots[4] = {r0, r1, r2, r3};
                Board bb(pz);
                for (int cell = 0; cell < 4; ++cell) { bb.pc[cell] = perm[cell]; bb.rt[cell] = rots[cell]; }
                bb.rebuild();
                bestCost = std::min(bestCost, (int)bb.cost);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
        // Bound at the root (nothing placed).
        std::vector<int> cells = {0, 1, 2, 3}, pool = {0, 1, 2, 3};
        std::vector<int> req(16, -1);
        for (int cell = 0; cell < 4; ++cell)
            for (int s = 0; s < 4; ++s)
                if (pz.neigh[cell * 4 + s] < 0) req[cell * 4 + s] = kGray;
        StubBoundResult br = StubMatchingBound(pz, cells, pool, req, nullptr);
        if (!br.infeasible && br.bound > bestCost) {
            std::cerr << "selftest: bound " << br.bound << " exceeds true optimum " << bestCost << "\n";
            return false;
        }
    }
    return true;
}

int CmdSelfTest(const Args& a) {
    std::cout << "engine: " << MatchingEngineName() << "\n";
    bool ok = true;
    const int trials = a.getInt("--trials", 40);
    std::cout << "[1/3] matching vs brute force ... "; std::cout.flush();
    ok &= SelfTestMatching(trials, 12, 999);
    std::cout << (ok ? "ok\n" : "FAILED\n");
    if (!ok) return 1;

    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"));
    std::cout << "[2/3] incremental scoring ... "; std::cout.flush();
    bool ok2 = SelfTestIncrements(pz);
    std::cout << (ok2 ? "ok\n" : "FAILED\n");
    ok &= ok2;

    std::cout << "[3/3] stub bound admissibility ... "; std::cout.flush();
    bool ok3 = SelfTestBound(40, 4242);
    std::cout << (ok3 ? "ok\n" : "FAILED\n");
    ok &= ok3;

    std::cout << (ok ? "\nall self-tests passed\n" : "\nSELF-TESTS FAILED\n");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { PrintUsage(argv[0]); return 1; }
    const std::string cmd = argv[1];
    const Args args = ParseArgs(argc, argv, 2);
    try {
        if (cmd == "solve") return CmdSolve(args);
        if (cmd == "verify") return CmdVerify(args);
        if (cmd == "bound") return CmdBound(args);
        if (cmd == "selftest") return CmdSelfTest(args);
        PrintUsage(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
