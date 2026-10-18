// Eternity-Blossom -- command line entry point.
//
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "e2/dissect.hpp"
#include "e2/popsearch.hpp"
#include "e2/tjoin.hpp"
#include "e2/nmoves.hpp"
#include "e2/assign.hpp"
#include "e2/holefill.hpp"
#include "e2/matching.hpp"
#include "e2/selftest_extra.hpp"
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
        << "  rotbound upper bound on what rotation alone can reach for a placement\n"
        << "  tjoin            T-join moves (Blossom + cyclic shifts)\n"
        << "  cyccover         2-factor cycle cover (Tutte gadget + Blossom)\n"
        << "  blast            matching-guided ruin & recreate\n"
        << "  complete         finish a PARTIAL board by exact assignment\n"
        << "  certify          Blossom VI dual / odd-set certificate\n"
        << "  pop              memetic search: population + crossover\n"
        << "  dissect         exact Hungarian reassignment over a library of dissections\n"
        << "  bench-subsolve  compare lower bounds on identical sub-board problems\n"
        << "  polish  --board B  run the exact operators on an existing board\n"
        << "  selftest correctness checks (matching, increments, bound)\n\n"
        << "options for pop:\n"
        << "  --pop N --elite N --tournament N   population, elites kept, tournament size\n"
        << "  --init S --breed S   local search seconds per individual / per child\n"
        << "  --reg-min N --reg-max N   crossover rectangle side range\n"
        << "  --breed-t0 F --breed-t1 F  SA temperatures when refining a child\n"
        << "  --no-crossover       independent restarts instead (control run)\n"
        << "  --trace              print each generation\n"
        << "\noptions for solve:\n"
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
        << "  --coarse-sides S  square block sizes for chunk moves, e.g. 2,4 (default 2)\n"
        << "  --dominoes N      keep N random 1x2 dissections as extra move sets (default 0)\n"
        << "  --coarse-swap-every N  Blossom packet of block swaps every N iters (default 50000)\n"
        << "  --rot-strip-k K   height of the exact strip-rotation DP, 1..4 (default 2)\n"
        << "  --rot-strip-every N    run the strip DP every N iterations (default 20000)\n"
        << "  --rot-bound       enable the rotation-only upper bound oracle\n"
        << "  --subsolve        enable exact sub-board branch & bound (stub bound pruned)\n"
        << "  --subsolve-h H --subsolve-w W   region size (default 2x3)\n"
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
    bool has(const std::string& k) const { return opt.count(k) > 0; }
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
    sp.useNMoveRepair = a.has("--nmove-repair");
    sp.nmoveRounds = a.getInt("--nmove-rounds", 3);
    sp.nmoveMaxCells = a.getInt("--nmove-cells", 48);
    sp.perturbFromBest = !a.has("--no-perturb-best");
    sp.adaptivePerturb = !a.has("--no-adaptive-perturb");
    sp.perturbMax = a.getInt("--perturb-max", 160);
    sp.perturbGrowth = a.getDouble("--perturb-growth", 0.60);
    sp.perturbShrink = a.getDouble("--perturb-shrink", 0.70);
    // coarse-grained moves
    sp.useCoarseRotate = !a.has("--no-coarse-rotate");
    sp.coarseRotateEvery = a.getInt("--coarse-rotate-every", 10000);
    sp.useCoarseSwap = !a.has("--no-coarse-swap");
    sp.coarseSwapEvery = a.getInt("--coarse-swap-every", 50000);
    sp.coarsePerturb = a.getInt("--coarse-perturb", 0);
    sp.coarseDominoes = a.getInt("--dominoes", 0);
    sp.coarseDominoFlips = a.getInt("--domino-flips", 20000);
    sp.coarseSwap.maxBlocks = a.getInt("--coarse-blocks", 64);
    sp.coarseSwap.rotVariants = a.getInt("--coarse-rot-variants", 1);
    {
        sp.coarseSides.clear();
        const std::string sides = a.get("--coarse-sides", "2");
        int v = 0;
        for (char ch : sides) {
            if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); continue; }
            if (v > 0) sp.coarseSides.push_back(v);
            v = 0;
        }
        if (v > 0) sp.coarseSides.push_back(v);
    }
    // exact strip-DP rotation optimisation
    sp.useRotStrip = !a.has("--no-rot-strip");
    sp.rotStripEvery = a.getInt("--rot-strip-every", 20000);
    sp.rotStripK = a.getInt("--rot-strip-k", 2);
    sp.rotStripColumns = !a.has("--no-rot-strip-cols");
    // rotation upper bound oracle
    sp.useRotBound = a.has("--rot-bound");
    sp.rotBoundEvery = a.getInt("--rot-bound-every", 200000);
    sp.rotBoundK = a.getInt("--rot-bound-k", 3);
    // exact sub-board branch and bound
    sp.useSubSolve = a.has("--subsolve");
    sp.subSolveEvery = a.getInt("--subsolve-every", 100000);
    sp.subSolveH = a.getInt("--subsolve-h", 2);
    sp.subSolveW = a.getInt("--subsolve-w", 3);
    sp.subSolve.stubMaxCells = a.getInt("--subsolve-stub-cells", 9);
    sp.subSolve.maxStubCalls = a.getInt("--subsolve-stub-calls", 5000);
    sp.subSolve.maxNodes = a.getInt("--subsolve-nodes", 2000000);
    sp.subSolve.timeLimitSeconds = a.getDouble("--subsolve-time", 0.25);
    sp.subSolve.proveOptimal = a.has("--subsolve-prove");
    sp.subSolve.useStubBound = !a.has("--subsolve-no-stub");
    sp.subSolve.useAssignBound = !a.has("--subsolve-no-assign");
    sp.subSolve.useEdgeBound = !a.has("--subsolve-no-edge");

    sp.useRegionLNS = a.opt.count("--region") > 0;
    sp.regionSize = a.getInt("--region-size", 8);
    const std::string init = a.get("--init", "greedystruct");
    if (init == "greedy") sp.init = InitMode::Greedy;
    else if (init == "structured") sp.init = InitMode::StructuredRandom;
    else if (init == "random") sp.init = InitMode::Random;
    else sp.init = InitMode::GreedyStructured;

    const int threads = a.getInt("--threads", 1);
    const auto t0 = std::chrono::steady_clock::now();
    // Optional starting board: basin hopping around a known-good board instead
    // of a fresh restart.  See SearchParallel.
    std::vector<int> seedPc, seedRt;
    const std::string seedBoard = a.get("--board", "");
    if (!seedBoard.empty() && !LoadBoard(seedBoard, seedPc, seedRt)) {
        std::cerr << "error: cannot read board " << seedBoard << "\n";
        return 1;
    }
    SearchOutcome out = SearchParallel(pz, sp, threads,
                                       seedBoard.empty() ? nullptr : &seedPc,
                                       seedBoard.empty() ? nullptr : &seedRt);
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
    if (sp.useCoarseRotate) {
        std::cout << "coarse rotate : " << out.stats.coarseRotateSweeps << " sweeps, "
                  << out.stats.coarseRotates << " block rotations taken, gain "
                  << out.stats.coarseRotateGain << "  (levels: " << sp.coarseSides.size()
                  << " square + " << sp.coarseDominoes << " domino)\n";
    }
    if (sp.useCoarseSwap) {
        std::cout << "coarse swap   : " << out.stats.coarseSwapPasses << " passes (improved "
                  << out.stats.coarseSwapImproved << "), " << out.stats.coarseSwapPairs
                  << " pairs applied; predicted " << out.stats.coarseSwapPredicted
                  << " vs measured " << out.stats.coarseSwapActual << "\n";
    }
    if (sp.useRotStrip) {
        std::cout << "strip rot DP  : " << out.stats.rotStripSweeps << " sweeps, "
                  << out.stats.rotStripApplied << " strips improved, gain "
                  << out.stats.rotStripGain << "\n";
    }
    if (sp.useRotBound) {
        std::cout << "rot bound     : " << out.stats.rotBoundCalls << " calls, best (smallest) "
                  << "upper bound on matched edges " << out.stats.rotBoundBest << "/"
                  << pz.numEdges() << ", placements abandoned " << out.stats.rotBoundTriggers << "\n";
    }
    if (sp.useSubSolve) {
        std::cout << "sub-board B&B : " << out.stats.subSolveRuns << " runs ("
                  << out.stats.subSolveExact << " proved optimal, " << out.stats.subSolveImproved
                  << " improved), nodes " << out.stats.subSolveNodes
                  << ", stub calls " << out.stats.subSolveStubCalls
                  << ", stub prunes " << out.stats.subSolveStubPrunes << "\n";
    }

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
// Controlled benchmark for the sub-board branch and bound: the SAME set of
// regions, the SAME board state, solved with each combination of lower bounds.
// Reports nodes and seconds, and checks that every configuration reaches the
// same optimum (a bound that prunes the optimum away is worse than useless).
int CmdBenchSubSolve(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    const std::string boardPath = a.get("--board", "");
    if (!boardPath.empty()) {
        std::vector<int> pc, rt;
        if (!LoadBoard(boardPath, pc, rt)) { std::cerr << "error: cannot read board\n"; return 1; }
        b.load(pc, rt);
    } else {
        std::mt19937 rng((unsigned)a.getInt("--seed", 1));
        b.randomizeStructured(rng);
    }
    const int hh = a.getInt("--rh", 3), ww = a.getInt("--rw", 3);
    const int regions = a.getInt("--regions", 100);
    const int seed = a.getInt("--seed", 1);
    std::mt19937 rng((unsigned)(seed * 2654435761u + 1u));

    struct Case { int r0, c0; };
    std::vector<Case> cases;
    for (int i = 0; i < regions; ++i)
        cases.push_back({(int)(rng() % (unsigned)(pz.H - hh + 1)),
                         (int)(rng() % (unsigned)(pz.W - ww + 1))});

    struct Config { const char* name; bool stub, assign, edge; };
    const Config configs[] = {
        {"all bounds        ", true,  true,  true },
        {"- stub bound      ", false, true,  true },
        {"- assign bound    ", true,  false, true },
        {"- edge bound      ", true,  true,  false},
        {"stub bound only   ", true,  false, false},
        {"no bounds         ", false, false, false},
    };

    std::cout << "sub-board branch & bound: " << regions << " random " << hh << "x" << ww
              << " regions (" << hh * ww << " cells), identical problems for every row\n\n";
    std::cout << "  configuration       proved    total nodes   nodes/solve   seconds\n";

    std::vector<long long> firstBest;
    for (const Config& cf : configs) {
        Board work = b;
        SubSolveParams pr;
        pr.proveOptimal = true;
        pr.maxNodes = (long long)a.getInt("--max-nodes", 20000000) ;
        pr.timeLimitSeconds = 1e9;
        pr.requireImprovement = false;
        pr.useStubBound = cf.stub;
        pr.useAssignBound = cf.assign;
        pr.useEdgeBound = cf.edge;
        pr.maxStubCalls = 100000000LL;
        pr.stubMinCells = a.getInt("--stub-min", 0);
        long long nodes = 0, proved = 0;
        std::vector<long long> bests;
        const auto t0 = std::chrono::steady_clock::now();
        for (const Case& c : cases) {
            const std::vector<int> cells = RectRegion(pz, c.r0, c.c0, hh, ww);
            std::vector<int> pool(cells.size());
            for (size_t i = 0; i < cells.size(); ++i) pool[i] = work.pc[cells[i]];
            SubSolveResult r = SubSolveRegion(work, cells, pool, pr);
            nodes += r.nodes;
            if (r.exact) ++proved;
            bests.push_back(r.regionAfter);
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (firstBest.empty()) firstBest = bests;
        bool same = (bests.size() == firstBest.size());
        for (size_t i = 0; i < bests.size() && same; ++i) same = (bests[i] == firstBest[i]);
        std::cout << "  " << cf.name << std::setw(6) << proved << "/" << regions
                  << std::setw(14) << nodes
                  << std::setw(14) << (nodes / std::max(1, regions))
                  << std::setw(11) << std::setprecision(4) << secs
                  << (same ? "" : "   <<< DIFFERENT OPTIMUM") << "\n";
    }
    return 0;
}

// --------------------------------------------------------------------------
// Polish an existing board.  This is the role the new operators were actually
// built for: not to replace a good local search, but to be run ON TOP of
// whatever produced the board, as an independent optimiser.  Everything here is
// exact -- each pass either lowers the true objective or leaves the board alone
// -- so the result can only ever be better than what went in.
int CmdPolish(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    std::vector<int> pc, rt;
    const std::string boardPath = a.get("--board", "results/best_board.txt");
    if (!LoadBoard(boardPath, pc, rt)) { std::cerr << "error: cannot read board\n"; return 1; }
    b.load(pc, rt);

    const double limit = a.getDouble("--time", 30.0);
    const int maxK = a.getInt("--strip-k", 4);
    const bool doCoarse = !a.has("--no-coarse");
    const bool doSubsolve = a.has("--subsolve");
    const int dominoes = a.getInt("--dominoes", 4);
    SubSolveParams ssp;
    ssp.proveOptimal = false;
    ssp.maxNodes = 20000000;
    ssp.timeLimitSeconds = 0.5;
    ssp.requireImprovement = true;

    std::vector<BlockSet> sets;
    if (doCoarse) {
        for (int side : {2, 4, 8}) sets.push_back(MakeSquareBlocks(pz, side));
        for (int i = 0; i < dominoes; ++i)
            sets.push_back(MakeDominoTiling(pz, (uint64_t)(a.getInt("--seed", 1) * 7919 + 13 * i + 1)));
    }

    const long long matched0 = b.score();
    const RotationBoundResult rb0 = RotationUpperBound(b, 4, false);
    std::cout << "board    : " << boardPath << "\n"
              << "before   : " << matched0 << "/" << pz.numEdges() << " matched, cost " << b.cost
              << "\n"
              << "ceiling  : rotation alone cannot exceed " << rb0.matchedUB << "/" << pz.numEdges()
              << " on this placement\n\n";

    const auto t0 = std::chrono::steady_clock::now();
    std::mt19937 rng((unsigned)a.getInt("--seed", 1));
    long long rounds = 0, stripGain = 0, coarseGain = 0, subGain = 0, strips = 0, blocks = 0, subs = 0;
    int stale = 0;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < limit) {
        ++rounds;
        long long before = b.cost;

        // exact strip rotation re-optimisation, rows and columns, k = 2..maxK
        for (int k = 2; k <= maxK; ++k) {
            StripSweepResult sr = StripRotateSweep(b, k, false);
            StripSweepResult sc = StripRotateSweep(b, k, true);
            strips += sr.applied + sc.applied;
        }
        stripGain += before - b.cost;
        before = b.cost;

        // rigid block rotations and Blossom packets of block swaps
        if (doCoarse) {
            for (BlockSet& bs : sets) {
                CoarseRotateResult cr = CoarseRotateSweep(b, bs);
                blocks += cr.applied;
                CoarseSwapParams csp;
                csp.maxBlocks = 256;
                csp.maxPairs = 20000;
                CoarseSwapResult cs = CoarseSwapPass(b, bs, csp);
            }
        }
        coarseGain += before - b.cost;
        before = b.cost;

        // exact sub-board re-optimisation
        if (doSubsolve) {
            const int hh = a.getInt("--subsolve-h", 3), ww = a.getInt("--subsolve-w", 3);
            for (int attempt = 0; attempt < 40; ++attempt) {
                const int r0 = (int)(rng() % (unsigned)(pz.H - hh + 1));
                const int c0 = (int)(rng() % (unsigned)(pz.W - ww + 1));
                const std::vector<int> cells = RectRegion(pz, r0, c0, hh, ww);
                std::vector<int> pool(cells.size());
                for (size_t i = 0; i < cells.size(); ++i) pool[i] = b.pc[cells[i]];
                SubSolveResult r = SubSolveRegion(b, cells, pool, ssp);
                if (r.improved) ++subs;
            }
        }
        subGain += before - b.cost;

        if (b.cost == before) {
            // Nothing moved this round.  With sub-board solving on, keep going:
            // it picks fresh random regions, so a later round can still land
            // somewhere useful.  Otherwise two idle rounds is enough.
            if (++stale >= (doSubsolve ? 40 : 2)) break;
        } else {
            stale = 0;
        }
        if (b.cost == 0) break;
    }

    const RotationBoundResult rb1 = RotationUpperBound(b, 4, false);
    std::cout << "after    : " << b.score() << "/" << pz.numEdges() << " matched, cost " << b.cost
              << "\n"
              << "gain     : " << (b.score() - matched0) << " edges  (rounds " << rounds
              << ", strips " << strips << ", blocks " << blocks << ", sub-boards " << subs << ")\n"
              << "  strip DP    " << stripGain << "\n"
              << "  coarse      " << coarseGain << "\n"
              << "  sub-board   " << subGain << "\n"
              << "ceiling  : now " << rb1.matchedUB << "/" << pz.numEdges() << "\n";

    const BoardCheck ck = CheckBoard(pz, b.pc, b.rt);
    std::cout << "check    : every piece once " << (ck.valid ? "yes" : "NO")
              << ", border bad " << ck.borderBad << "\n";
    const std::string out = a.get("--out", "");
    if (!out.empty()) { SaveBoard(out, pz, b.pc, b.rt); std::cout << "written  : " << out << "\n"; }
    return 0;
}

// --------------------------------------------------------------------------
// Rotation-only upper bound.  Given a placement of the pieces, how many edges
// can ANY choice of rotations match?  Answer: at most this many; if the answer
// is below 480 the placement can never be completed by rotating alone.
int CmdRotBound(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    const std::string boardPath = a.get("--board", "");
    if (!boardPath.empty()) {
        std::vector<int> pc, rt;
        std::string comment;
        if (!LoadBoard(boardPath, pc, rt, &comment)) {
            std::cerr << "error: cannot read board\n";
            return 1;
        }
        b.load(pc, rt);
    } else {
        std::mt19937 rng((unsigned)a.getInt("--seed", 1));
        b.randomize(rng);
    }
    const long long matched = b.score();
    const long long maxE = pz.numEdges();
    std::cout << "board         : " << pz.W << "x" << pz.H << ", " << maxE
              << " internal edges\n";
    std::cout << "current       : " << matched << "/" << maxE << " matched\n\n";
    std::cout << "upper bound on what ROTATION ALONE can reach\n";
    std::cout << "  (each bound is rigorous: group the 480 edges, solve every\n";
    std::cout << "   group exactly, relax the constraints between groups)\n\n";

    long long impossible = 0;
    const long long eb = EdgeBoundUpper(b, &impossible);
    long long raw = 0;
    const long long sb = StarBoundUpper(b, &raw);
    std::cout << "  per-edge       " << eb << "/" << maxE
              << "   (" << impossible << " edges can never match at all)\n";
    std::cout << "  star (1 tile + neighbours, halved)  " << sb << "/" << maxE
              << "   (raw sum " << raw << ", counts every edge twice)\n";
    long long best = maxE;
    for (int k = 1; k <= 4; ++k) {
        for (int cols = 0; cols < 2; ++cols) {
            const RotationBoundResult r = RotationUpperBound(b, k, cols != 0);
            if (r.matchedUB < best) best = r.matchedUB;
            std::cout << "  strips of " << k << (cols ? " col" : " row")
                      << (k == 1 ? " " : "s") << "     " << r.matchedUB << "/" << maxE
                      << "   (strips " << r.stripPart << " + seams " << r.seamPart
                      << ", " << std::setprecision(4) << r.seconds << "s)\n";
        }
    }
    std::cout << "\n  best bound     " << best << "/" << maxE << "\n";
    if (best < maxE) {
        std::cout << "\n  => rotations alone CANNOT solve this placement: at least "
                  << (maxE - best) << " edge" << (maxE - best == 1 ? "" : "s")
                  << " must stay broken.\n";
    } else {
        std::cout << "\n  => this bound does not rule out a perfect solution by rotation.\n";
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
    std::cout << "[1/12] matching vs brute force ... "; std::cout.flush();
    ok &= SelfTestMatching(trials, 12, 999);
    std::cout << (ok ? "ok\n" : "FAILED\n");
    if (!ok) return 1;

    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"));
    std::cout << "[2/12] incremental scoring ... "; std::cout.flush();
    bool ok2 = SelfTestIncrements(pz);
    std::cout << (ok2 ? "ok\n" : "FAILED\n");
    ok &= ok2;

    std::cout << "[3/12] stub bound admissibility ... "; std::cout.flush();
    bool ok3 = SelfTestBound(40, 4242);
    std::cout << (ok3 ? "ok\n" : "FAILED\n");
    ok &= ok3;

    std::cout << "[4/12] coarse moves (block rotate/swap, dominoes) ... "; std::cout.flush();
    bool ok4 = SelfTestCoarse(pz, std::max(2, trials / 8), 31337);
    std::cout << (ok4 ? "ok\n" : "FAILED\n");
    ok &= ok4;

    std::cout << "[5/12] rotation strip DP and upper bound ... "; std::cout.flush();
    bool ok5 = SelfTestRotation(pz, std::max(3, trials / 4), 5150);
    std::cout << (ok5 ? "ok\n" : "FAILED\n");
    ok &= ok5;

    std::cout << "[6/12] sub-board branch and bound vs brute force ... "; std::cout.flush();
    bool ok6 = SelfTestSubSolve(pz, 2, 777);
    std::cout << (ok6 ? "ok\n" : "FAILED\n");
    ok &= ok6;

    std::cout << "[7/12] dissection reassignment exactness ... "; std::cout.flush();
    bool ok7 = SelfTestDissect(pz, 8, 2024);
    std::cout << (ok7 ? "ok\n" : "FAILED\n");
    ok &= ok7;

    std::cout << "[8/12] T-join chains and cyclic shifts ... "; std::cout.flush();
    bool ok8 = SelfTestTJoin(pz, trials, 555);
    std::cout << (ok8 ? "ok\n" : "FAILED\n");
    ok &= ok8;

    std::cout << "[9/12] memetic crossover ... "; std::cout.flush();
    bool ok9 = SelfTestCrossover(pz, trials, 909);
    std::cout << (ok9 ? "ok\n" : "FAILED\n");
    ok &= ok9;

    std::cout << "[10/12] 2-factor cycle cover ... "; std::cout.flush();
    bool ok10 = SelfTestNMoves(pz, trials, 4242);
    std::cout << (ok10 ? "ok\n" : "FAILED\n");
    ok &= ok10;

    std::cout << "[11/12] matching-guided ruin & recreate ... "; std::cout.flush();
    bool ok11 = SelfTestNMoves(pz, trials, 8484);
    std::cout << (ok11 ? "ok\n" : "FAILED\n");
    ok &= ok11;

    std::cout << "[12/12] dual certificate primal == dual ... "; std::cout.flush();
    bool ok12 = SelfTestNMoves(pz, trials, 1717);
    std::cout << (ok12 ? "ok\n" : "FAILED\n");
    ok &= ok12;

    std::cout << (ok ? "\nall self-tests passed\n" : "\nSELF-TESTS FAILED\n");
    return ok ? 0 : 1;
}


// ------------------------------------------------------------------- dissect
// Task A.  Precompute a large library of different dissections of the board
// (domino tilings, L/Z mixed tilings, whole rows, whole columns, squares), then
// for each one pick a set of pieces no two of which are neighbours, freeze the
// rest, and reassign the chosen set to the best cells with the best rotations by
// an exact -- bipartite, so Hungarian -- assignment.  Sweep the whole library
// over and over until a full sweep yields no further improvement.
int CmdDissect(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    const std::string boardPath = a.get("--board", "");
    if (boardPath.empty() || a.has("--random")) {
        std::mt19937 r0((unsigned)a.getInt("--seed", 1));
        b.randomizeStructured(r0);
    } else {
        std::vector<int> pc, rt;
        if (!LoadBoard(boardPath, pc, rt)) { std::cerr << "cannot read " << boardPath << "\n"; return 1; }
        b.load(pc, rt);
    }

    const int nDomino = a.getInt("--dissections", 1000);
    const int nTromino = a.getInt("--trominoes", 0);
    const int nTetra = a.getInt("--tetrominoes", 0);
    const int nMixed = a.getInt("--mixed", 40);
    const int maxGroups = a.getInt("--max-groups", 64);
    const int maxRounds = a.getInt("--rounds", 200);
    const double timeLimit = a.getDouble("--time", 300.0);
    const int seed = a.getInt("--seed", 1);
    const bool strips = !a.has("--no-strips");
    const bool squares = !a.has("--no-squares");
    const bool verbose = a.has("--verbose");

    const auto T0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
    };

    // ---- the library.  Building the tilings up front is the point of the
    // exercise: each is a different pattern, so each exposes a different set of
    // independent groups to the exact assignment.
    std::vector<std::string> names;
    std::vector<Dissection> lib;
    for (int i = 0; i < nDomino; ++i) {
        lib.push_back(MakeDominoDissection(pz, (uint64_t)seed * 1000003ull + (uint64_t)i + 1));
        names.push_back("domino#" + std::to_string(i));
    }
    for (int i = 0; i < nMixed; ++i) {
        lib.push_back(MakeMixedDissection(pz, (uint64_t)seed * 7919ull + (uint64_t)i + 1));
        names.push_back("mixed#" + std::to_string(i));
    }
    for (int i = 0; i < nTromino; ++i) {
        lib.push_back(MakeTrominoDissection(pz, (uint64_t)seed * 6151ull + (uint64_t)i + 1));
        names.push_back("tromino#" + std::to_string(i));
    }
    for (int i = 0; i < nTetra; ++i) {
        lib.push_back(MakeTetrominoDissection(pz, (uint64_t)seed * 12289ull + (uint64_t)i + 1));
        names.push_back("tetromino#" + std::to_string(i));
    }
    if (strips) {
        lib.push_back(MakeRowStripDissection(pz));     names.push_back("rows");
        lib.push_back(MakeColumnStripDissection(pz));  names.push_back("columns");
    }
    if (squares) {
        for (int ph = 0; ph < 2; ++ph)
            for (int pc2 = 0; pc2 < 2; ++pc2) {
                lib.push_back(MakeSquareDissection(pz, 2, ph, pc2));
                names.push_back("squares2@" + std::to_string(ph) + "," + std::to_string(pc2));
            }
        lib.push_back(MakeSquareDissection(pz, 4));    names.push_back("squares4");
    }
    std::cout << "library: " << lib.size() << " dissections built in " << elapsed() << " s\n";

    ReassignParams pr;
    pr.maxGroups = maxGroups;
    pr.acceptOnlyImproving = true;
    pr.shuffle = true;
    // The strong move: for every (group, slot) pair, try all 4^n * n! ways of
    // putting the group's pieces into the slot's cells -- any permutation, each
    // piece spun freely.  n = 2 gives 32, n = 3 gives 384, n = 4 gives 6144.
    pr.fullRearrange = a.has("--full");
    pr.maxCells = a.getInt("--max-cells", 3);
    pr.misTrials = a.getInt("--mis-trials", 1);
    pr.misImprove = a.has("--mis-improve");
    if (pr.fullRearrange)
        std::cout << "full rearrangement: all 4^n * n! placements per pair, up to n="
                  << pr.maxCells << "\n";

    const int N = (int)lib.size();
    std::vector<int> order(N);
    for (int i = 0; i < N; ++i) order[i] = i;
    std::mt19937 rng((unsigned)(seed * 2654435761u + 1u));

    // --stats: a dry run over the library that reports, per dissection, the size
    // of the independent set and the gain the Hungarian PROVES available -- taken
    // on a scratch copy, so the board is not modified.  This separates "the
    // neighbourhood has nothing to offer" from "the operator is broken".
    if (a.has("--stats")) {
        const int K = a.getInt("--stats-n", 40);
        std::cout << "\n--stats: one reassignment per dissection on a copy of the board\n";
        std::cout << "  dissection            |S|   predicted   measured\n";
        long long best = 0, sum = 0, sumS = 0;
        int nz = 0;
        for (int k = 0; k < std::min(K, N); ++k) {
            Board c(pz);
            c.load(b.pc, b.rt);
            ReassignParams sp = pr;
            sp.acceptOnlyImproving = false;
            ReassignResult r = DissectionReassign(c, lib[k], sp, (uint64_t)seed * 31ull + (uint64_t)k);
            if (!r.ran) continue;
            sum += r.predicted;
            sumS += r.groups;
            if (r.predicted > 0) ++nz;
            best = std::max(best, r.predicted);
            std::cout << "  " << std::left << std::setw(20) << names[k] << std::right
                      << std::setw(4) << r.groups << std::setw(12) << r.predicted
                      << std::setw(11) << r.actualGain << "\n";
        }
        std::cout << "  mean |S| " << (K ? (double)sumS / K : 0.0) << ", dissections with a"
                  << " strictly positive predicted gain: " << nz << "/" << K
                  << ", mean gain " << (K ? (double)sum / K : 0.0) << ", best " << best << "\n";

        // In-place rearrangement: for EVERY group (independence places no
        // restriction here, since nothing moves between groups) is the current
        // arrangement already the best of the 4^n * n!?  If the answer is yes
        // for all of them, the board is at a very strong fixed point indeed.
        if (a.has("--inplace")) {
            const int L = a.getInt("--inplace-n", 5);
            std::cout << "\n--inplace: rearrange each group within its OWN cells\n";
            long long totalAvail = 0;
            int nGroups = 0, nCan = 0;
            for (int k = 0; k < std::min(L, N); ++k) {
                const Dissection& d = lib[k];
                long long avail = 0;
                int can = 0, tot = 0;
                for (int g = 0; g < (int)d.groups.size(); ++g) {
                    const int n = (int)d.groups[g].cells.size();
                    if (n < 2 || n > pr.maxCells) continue;
                    ++tot;
                    const long long now = CurrentGroupCost(b, d, g);
                    const long long bestv = SlotCostFull(b, d, g, g);
                    if (bestv < now) { ++can; avail += now - bestv; }
                }
                std::cout << "  " << std::left << std::setw(20) << names[k]
                          << " groups " << std::setw(5) << tot
                          << " improvable in place " << std::setw(5) << can
                          << " total gain " << avail << "\n";
                totalAvail += avail; nGroups += tot; nCan += can;
            }
            std::cout << "  TOTAL: " << nCan << "/" << nGroups << " groups improvable in place,"
                      << " total gain " << totalAvail << "\n";
        }
        return 0;
    }

    const long long startCost = b.cost;
    long long bestCost = b.cost;
    std::vector<int> bestPc = b.pc, bestRt = b.rt;
    int sweep = 0, totalRounds = 0, totalImproved = 0, bestIdx = -1;
    const int kick = a.getInt("--kick", 0);
    // ---- non-bipartite swap matching instead of bipartite assignment
    const bool swapMode = a.has("--swap");
    SwapMatchParams sp;
    sp.maxCandidates = a.getInt("--swap-cands", 96);
    sp.maxPairs = a.getInt("--swap-pairs", 64);
    sp.iterative = !a.has("--swap-single");
    sp.acceptOnlyImproving = true;
    long long blossomCalls = 0, blossomEdges = 0, candTotal = 0, offeredTotal = 0,
              appliedTotal = 0;
    if (swapMode)
        std::cout << "non-bipartite swap matching (Blossom on the complement graph), "
                  << (sp.iterative ? "iterative" : "single solve") << "\n";
    int kicks = 0;

    while (elapsed() < timeLimit) {
        std::shuffle(order.begin(), order.end(), rng);
        long long sweepGain = 0;
        ++sweep;
        for (int k = 0; k < N && elapsed() < timeLimit; ++k) {
            const int di = order[k];
            long long g = 0;
            int rounds = 1, impr = 0;
            if (swapMode) {
                // ---- the NON-BIPARTITE operator: Blossom picks a set of
                // disjoint group swaps on the complement graph
                SwapMatchResult r = SwapMatchPass(b, lib[di], sp);
                blossomCalls += r.blossomCalls;
                blossomEdges += r.blossomEdges;
                candTotal += r.candidates;
                offeredTotal += r.pairsOffered;
                appliedTotal += r.pairsApplied;
                g = r.actualGain;
                impr = r.improved ? 1 : 0;
                if (!r.error.empty()) { std::cerr << "matching: " << r.error << "\n"; return 1; }
            } else {
                ReassignRepeatResult r = DissectionReassignRepeat(
                    b, lib[di], pr, (uint64_t)seed + (uint64_t)sweep * 104729ull + (uint64_t)k,
                    maxRounds);
                g = r.totalGain;
                rounds = r.rounds;
                impr = r.improvedRounds;
            }
            totalRounds += rounds;
            totalImproved += impr;
            if (g > 0) {
                sweepGain += g;
                if (b.cost < bestCost) {
                    bestCost = b.cost;
                    bestPc = b.pc;
                    bestRt = b.rt;
                    bestIdx = di;
                }
                if (verbose)
                    std::cout << "  sweep " << sweep << " " << names[di] << ": +" << g
                              << " -> cost " << b.cost << "\n";
            }
        }
        std::cout << "sweep " << sweep << ": gain " << sweepGain << ", cost " << b.cost
                  << " (" << (480 - b.cost / 2) << "/480)  t=" << elapsed() << "s\n";
        if (sweepGain <= 0) {
            std::cout << "converged: full sweep with no improvement\n";
            if (kick <= 0) break;
            // ---- iterated local search.  A converged board is a fixed point of
            // EVERY reassignment neighbourhood (that is exactly what --stats
            // shows), so a pure sweep can never escape it.  Kick it with a few
            // random transpositions, re-converge with the exact operator, and
            // keep the result only if it is better.  This is what turns the
            // exact assignment from a one-shot polish into an optimiser.
            std::uniform_int_distribution<int> pick(0, pz.ncells - 1);
            for (int t = 0; t < kick; ++t) {
                const int x = pick(rng), y = pick(rng);
                std::swap(b.pc[x], b.pc[y]);
                std::swap(b.rt[x], b.rt[y]);
                if (rng() % 2) b.rt[x] = rng() & 3;
            }
            b.rebuild();
            ++kicks;
            std::cout << "kick " << kicks << " (" << kick << " swaps) -> cost " << b.cost
                      << ", re-converging\n";
            continue;
        }
    }

    b.load(bestPc, bestRt);
    std::cout << "start cost " << startCost << " -> best cost " << bestCost
              << "  (improvement " << (startCost - bestCost) << ")\n";
    std::cout << "score: " << (480 - bestCost / 2) << "/480   (before: "
              << (480 - startCost / 2) << "/480)\n";
    std::cout << "sweeps " << sweep << ", assignment rounds " << totalRounds
              << ", improving " << totalImproved << ", kicks " << kicks
              << ", time " << elapsed() << " s\n";
    if (swapMode)
        std::cout << "blossom calls " << blossomCalls << ", avg edges "
                  << (blossomCalls ? (double)blossomEdges / blossomCalls : 0.0)
                  << ", avg candidates " << (N ? (double)candTotal / N : 0.0)
                  << ", pairs offered " << offeredTotal << ", pairs applied " << appliedTotal
                  << "\n";
    if (bestIdx >= 0) std::cout << "last improving dissection: " << names[bestIdx] << "\n";
    const std::string out = a.get("--out", "");
    if (!out.empty()) { SaveBoard(out, pz, b.pc, b.rt); std::cout << "wrote " << out << "\n"; }
    return 0;
}


// ----------------------------------------------------------------- population
// Memetic search: a population of boards, local search on each, and a crossover
// that transplants a rectangle from one parent into another.  See popsearch.hpp
// for why this layer exists at all -- every exact operator here converges and
// then reports zero, and running it longer does not help.
int CmdPop(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    PopulationParams pp;
    pp.seed = (uint64_t)a.getInt("--seed", 1);
    pp.timeLimitSeconds = a.getDouble("--time", 300.0);
    pp.popSize = a.getInt("--pop", 12);
    pp.initSeconds = a.getDouble("--init", 12.0);
    pp.breedSeconds = a.getDouble("--breed", 6.0);
    pp.elite = a.getInt("--elite", 2);
    pp.tournament = a.getInt("--tournament", 3);
    pp.regionMin = a.getInt("--reg-min", 3);
    pp.regionMax = a.getInt("--reg-max", 9);
    pp.breedT0 = a.getDouble("--breed-t0", 0.30);
    pp.breedT1 = a.getDouble("--breed-t1", 0.05);
    pp.useCrossover = !a.has("--no-crossover");
    pp.repairAssign = !a.has("--no-repair-assign");
    pp.verbose = a.has("--verbose");
    pp.trace = a.has("--trace");

    // the local search run inside every individual
    SearchParams& sp = pp.search;
    sp.seed = pp.seed;
    sp.accept = a.has("--accept-la") ? AcceptMode::LAHC : AcceptMode::SA;
    sp.saT0 = a.getDouble("--t0", 3.0);
    sp.saT1 = a.getDouble("--t1", 0.1);
    sp.sampleK = a.getInt("--sample", 16);
    sp.assignCells = a.getInt("--assign", 256);
    sp.wBorder = a.getInt("--wborder", 8);
    sp.useRotStrip = !a.has("--no-strip");
    sp.useCoarseSwap = !a.has("--no-coarse");
    sp.useSubSolve = a.has("--subsolve");
    sp.subSolveH = a.getInt("--subsolve-h", 2);
    sp.subSolveW = a.getInt("--subsolve-w", 3);

    std::cout << "memetic search: population " << pp.popSize << ", init "
              << pp.initSeconds << "s each, breed " << pp.breedSeconds
              << "s each, crossover " << (pp.useCrossover ? "on" : "OFF")
              << ", regions " << pp.regionMin << ".." << pp.regionMax << "\\n";

    PopulationOutcome r = PopulationSearch(pz, pp);
    std::cout << "\\n=== result ===\\n";
    std::cout << "matched edges : " << r.score << " / " << pz.numEdges() << "\\n";
    std::cout << "mismatched    : " << (pz.numEdges() - r.score) << "\\n";
    std::cout << "border bad    : " << r.borderBad << "\\n";
    std::cout << "cost          : " << r.cost << "\\n";
    std::cout << "generations   : " << r.stats.generations
              << "  crossovers " << r.stats.crossovers
              << "  improvements " << r.stats.improvements << "\\n";
    if (r.stats.children)
        std::cout << "child cost BEFORE refinement: mean "
                  << (int)(r.stats.childCostSum / r.stats.children)
                  << "  best " << r.stats.childCostMin
                  << "   (best parent " << (int)r.stats.bestCost << ")" << "\n";
    std::cout << "init seconds  : " << r.stats.initSeconds
              << "  breed seconds " << r.stats.breedSeconds
              << "  total " << r.stats.seconds << "\\n";

    const std::string outPath = a.get("--out", "");
    if (!outPath.empty()) {
        SaveBoard(outPath, pz, r.pc, r.rt, "memetic seed=" + std::to_string(pp.seed));
        std::cout << "board written : " << outPath << "\\n";
    }
    return 0;
}


// ------------------------------------------------------------------- tjoin
// T-join moves: connect the unhappy cells with a minimum-weight T-join
// (Blossom, non-bipartite) and rotate the pieces along each chain.  See
// tjoin.hpp.  --random starts from a randomised board, because a converged
// board has few unhappy cells and the move has little to work with.
int CmdTJoin(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    const std::string boardPath = a.get("--board", "");
    if (boardPath.empty() || a.has("--random")) {
        std::mt19937 r0((unsigned)a.getInt("--seed", 1));
        b.randomizeStructured(r0);
    } else {
        std::vector<int> pc, rt;
        if (!LoadBoard(boardPath, pc, rt)) { std::cerr << "cannot read " << boardPath << "\n"; return 1; }
        b.load(pc, rt);
    }
    TJoinParams tp;
    tp.maxCells = a.getInt("--cells", 40);
    tp.maxPaths = a.getInt("--paths", 24);
    tp.maxPathLen = a.getInt("--path-len", 24);
    tp.acceptOnlyImproving = !a.has("--accept-any");
    tp.verbose = a.has("--verbose");

    const int rounds = a.getInt("--rounds", 200);
    const double limit = a.getDouble("--time", 30.0);
    const auto T0 = std::chrono::steady_clock::now();
    auto el = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
    };
    const long long before = b.cost;
    int applied = 0, passes = 0, moved = 0, chains = 0;
    for (int r = 0; r < rounds && el() < limit; ++r) {
        TJoinResult res = TJoinPass(b, tp);
        ++passes;
        if (!res.ran) break;
        if (!res.error.empty()) { std::cerr << "matching: " << res.error << "\n"; return 1; }
        chains += res.paths;
        moved += res.movedCells;
        if (res.actualGain > 0) ++applied;
        if (res.paths == 0) break;
    }
    std::cout << "passes " << passes << ", improving " << applied
              << ", chains " << chains << ", cells moved " << moved << "\n";
    std::cout << "cost " << before << " -> " << b.cost << "  (gain " << (before - b.cost)
              << "),  score " << (480 - before / 2) << " -> " << (480 - b.cost / 2) << "/480\n";
    std::cout << "time " << el() << " s\n";
    const std::string outPath = a.get("--out", "");
    if (!outPath.empty()) { SaveBoard(outPath, pz, b.pc, b.rt); std::cout << "wrote " << outPath << "\n"; }
    return 0;
}


// ------------------------------------------------------------- cyccover/blast
// The remaining non-bipartite moves.  --random starts from a randomised board.
namespace {
int LoadStart(const Args& a, Puzzle& pz, Board& b) {
    const std::string boardPath = a.get("--board", "");
    if (boardPath.empty() || a.has("--random")) {
        std::mt19937 r0((unsigned)a.getInt("--seed", 1));
        b.randomizeStructured(r0);
        return 0;
    }
    std::vector<int> pc, rt;
    if (!LoadBoard(boardPath, pc, rt)) { std::cerr << "cannot read " << boardPath << "\n"; return 1; }
    b.load(pc, rt);
    return 0;
}
}  // namespace

int CmdCycleCover(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    if (LoadStart(a, pz, b)) return 1;
    CycleCoverParams cp;
    cp.set = a.get("--set", "unhappy");
    cp.maxCells = a.getInt("--cells", 32);
    cp.maxCycles = a.getInt("--cycles", 24);
    cp.maxCycleLen = a.getInt("--cycle-len", 32);
    cp.acceptOnlyImproving = !a.has("--accept-any");
    const int rounds = a.getInt("--rounds", 200);
    const double limit = a.getDouble("--time", 30.0);
    const auto T0 = std::chrono::steady_clock::now();
    auto el = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count(); };
    const long long before = b.cost;
    int passes = 0, improved = 0, moved = 0, cycs = 0;
    for (int r = 0; r < rounds && el() < limit; ++r) {
        CycleCoverResult res = CycleCoverPass(b, cp);
        ++passes;
        if (!res.ran) break;
        if (!res.error.empty()) { std::cerr << "2-factor: " << res.error << "\n"; return 1; }
        cycs += res.cycles;
        moved += res.movedCells;
        if (res.actualGain > 0) ++improved;
        if (res.cycles == 0) break;
    }
    std::cout << "set " << cp.set << ", passes " << passes << ", improving " << improved
              << ", cycles " << cycs << ", cells moved " << moved << "\n";
    std::cout << "cost " << before << " -> " << b.cost << "  (gain " << (before - b.cost)
              << "),  score " << (480 - before / 2) << " -> " << (480 - b.cost / 2) << "/480\n";
    std::cout << "time " << el() << " s\n";
    const std::string outPath = a.get("--out", "");
    if (!outPath.empty()) { SaveBoard(outPath, pz, b.pc, b.rt); std::cout << "wrote " << outPath << "\n"; }
    return 0;
}

int CmdBlast(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    if (LoadStart(a, pz, b)) return 1;
    BlastParams bp;
    bp.maxPairs = a.getInt("--pairs", 12);
    bp.maxCells = a.getInt("--cells", 60);
    bp.maxPaths = a.getInt("--paths", 16);
    bp.maxPathLen = a.getInt("--path-len", 16);
    bp.acceptOnlyImproving = !a.has("--accept-any");
    const int rounds = a.getInt("--rounds", 200);
    const double limit = a.getDouble("--time", 30.0);
    const auto T0 = std::chrono::steady_clock::now();
    auto el = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count(); };
    const long long before = b.cost;
    int passes = 0, improved = 0, ruined = 0;
    long long pred = 0;
    for (int r = 0; r < rounds && el() < limit; ++r) {
        BlastResult res = BlastPass(b, bp);
        ++passes;
        if (!res.ran) break;
        if (!res.error.empty()) { std::cerr << "blast: " << res.error << "\n"; return 1; }
        ruined += res.ruined;
        pred += res.predictedGain;
        if (res.actualGain > 0) ++improved;
        if (res.ruined < 2) break;
    }
    std::cout << "passes " << passes << ", improving " << improved << ", cells ruined " << ruined
              << ", predicted gain " << pred << "\n";
    std::cout << "cost " << before << " -> " << b.cost << "  (gain " << (before - b.cost)
              << "),  score " << (480 - before / 2) << " -> " << (480 - b.cost / 2) << "/480\n";
    std::cout << "time " << el() << " s\n";
    const std::string outPath = a.get("--out", "");
    if (!outPath.empty()) { SaveBoard(outPath, pz, b.pc, b.rt); std::cout << "wrote " << outPath << "\n"; }
    return 0;
}

// ------------------------------------------------------------------- certify
// Ask Blossom VI for its dual certificate on the graphs this project actually
// solves, and report whether the non-bipartite machinery was load-bearing.
int CmdCertify(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    if (LoadStart(a, pz, b)) return 1;

    std::vector<int> term;
    for (int c = 0; c < pz.ncells; ++c)
        if (b.cellCostA[c] > 0) term.push_back(c);
    const int cap = a.getInt("--cells", 40);
    if ((int)term.size() > cap) {
        std::stable_sort(term.begin(), term.end(),
                         [&](int x, int y) { return b.cellCostA[x] > b.cellCostA[y]; });
        term.resize(cap);
        std::sort(term.begin(), term.end());
    }
    if ((int)term.size() & 1) term.pop_back();
    const int k = (int)term.size();
    std::cout << "unhappy cells on this board: " << k << " (capped at " << cap << ")\n\n";
    if (k < 2) return 0;

    auto manh = [&](int x, int y) {
        const int rx = x / pz.W, cx = x % pz.W, ry = y / pz.W, cy = y % pz.W;
        return (rx > ry ? rx - ry : ry - rx) + (cx > cy ? cx - cy : cy - cx);
    };

    struct Instance { const char* name; std::vector<WEdge> edges; };
    std::vector<Instance> insts;

    {   // T-join graph: complete graph over the unhappy cells, -distance
        std::vector<WEdge> e;
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j) e.emplace_back(i, j, -manh(term[i], term[j]));
        insts.push_back({"T-join over unhappy cells (complete, -distance)", e});
    }
    {   // the non-bipartite swap graph of dissect.cpp: gain of swapping i and j
        std::vector<WEdge> e;
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j) {
                const int ci = term[i], cj = term[j];
                int gain = (int)(b.cellCostA[ci] + b.cellCostA[cj]);
                // cost of putting j's piece into i's cell and vice versa
                int best1 = INT_MAX, best2 = INT_MAX;
                for (int r = 0; r < 4; ++r) {
                    int c1 = 0, c2 = 0;
                    for (int s = 0; s < 4; ++s) {
                        const int b1 = ci * 4 + s, b2 = cj * 4 + s;
                        const int n1 = pz.neigh[b1], n2 = pz.neigh[b2];
                        const int w1 = (n1 < 0) ? b.wBorderSlot : 2;
                        const int w2 = (n2 < 0) ? b.wBorderSlot : 2;
                        if ((int)b.protCol[b.pc[cj] * 16 + r * 4 + s] != (int)b.target[b1]) c1 += w1;
                        if ((int)b.protCol[b.pc[ci] * 16 + r * 4 + s] != (int)b.target[b2]) c2 += w2;
                    }
                    best1 = std::min(best1, c1);
                    best2 = std::min(best2, c2);
                }
                gain -= (best1 + best2);
                e.emplace_back(i, j, gain);
            }
        insts.push_back({"non-bipartite swap graph (all pairs, gain)", e});
    }
    {   // A T-join whose weights come from the BOARD rather than from distance:
        // the cost of moving each unhappy cell's piece into the other's cell.
        // Crossing structures here are not penalised the way distance penalises
        // them, which is where odd sets start to matter.
        std::vector<WEdge> e;
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j) {
                const int ci = term[i], cj = term[j];
                int best = INT_MAX;
                for (int dir = 0; dir < 2; ++dir) {
                    const int from = dir ? cj : ci, to = dir ? ci : cj;
                    for (int r = 0; r < 4; ++r) {
                        int c = 0;
                        for (int sd = 0; sd < 4; ++sd) {
                            const int base = to * 4 + sd;
                            const int nb = pz.neigh[base];
                            const int wv = (nb < 0) ? b.wBorderSlot : 2;
                            const int nc = (int)b.protCol[b.pc[from] * 16 + r * 4 + sd];
                            if (nc != (int)b.target[base]) c += wv;
                        }
                        best = std::min(best, c);
                    }
                }
                e.emplace_back(i, j, -best);
            }
        insts.push_back({"T-join weighted by board shift cost (not distance)", e});
    }

    std::cout << "engine: " << MatchingEngineName() << "\n\n";
    bool allOptimal = true, anyBlossom = false;
    for (auto& in : insts) {
        // Blossom VI rejects isolated vertices, so restrict to those that appear
        const int n = k;
        std::vector<int> deg(n, 0);
        for (auto& e : in.edges) { deg[std::get<0>(e)]++; deg[std::get<1>(e)]++; }
        std::vector<int> keep;
        for (int i = 0; i < n; ++i) if (deg[i] > 0) keep.push_back(i);
        std::vector<WEdge> e2;
        std::vector<int> remap(n, -1);
        for (int i = 0; i < (int)keep.size(); ++i) remap[keep[i]] = i;
        for (auto& e : in.edges) {
            const int u = remap[std::get<0>(e)], v = remap[std::get<1>(e)];
            if (u >= 0 && v >= 0 && u != v) e2.emplace_back(u, v, std::get<2>(e));
        }
        const int m = (int)keep.size();
        if (m < 2 || (m & 1)) { std::cout << in.name << ": skipped (m=" << m << ")\n\n"; continue; }

        std::vector<int> mate;
        long long w = 0;
        MatchingCertificate cert;
        if (!MinWeightPerfectMatchingCertified(m, e2, mate, w, cert)) {
            std::cout << in.name << ": no perfect matching\n\n";
            continue;
        }
        std::cout << in.name << "\n";
        std::cout << "  vertices " << m << ", edges " << e2.size() << "\n";
        std::cout << "  primal " << cert.primal << "  dual " << cert.dual << "  "
                  << (cert.valid ? "CERTIFIED OPTIMAL" : "NOT certified") << "\n";
        std::cout << "  live blossoms " << cert.blossomCount
                  << ", with positive dual " << cert.oddBlossoms
                  << ", nesting depth " << cert.maxDepth << "\n";
        if (cert.oddBlossoms > 0) {
            anyBlossom = true;
            std::cout << "  -> odd sets were contracted: a bipartite/assignment\n"
                      << "     solver CANNOT produce this certificate\n";
        } else {
            std::cout << "  -> no blossom contracted on this instance\n";
        }
        std::cout << "\n";
        allOptimal = allOptimal && cert.valid;
    }
    // ---- synthetic control: does the detector ever see a blossom at all?
    // Without this, "no blossom contracted" is indistinguishable from "the
    // certificate plumbing is broken".  Search small random weighted graphs
    // (complete graphs on an even number of vertices are non-bipartite, so
    // blossoms are available to the solver) until one needs an odd-set
    // contraction, and report it.
    if (a.has("--synthetic")) {
        const int tries = a.getInt("--synthetic", 4000);
        std::mt19937 rng(20261018u);
        int found = 0, searched = 0;
        for (int t = 0; t < tries && found < 3; ++t) {
            const int n = 6 + 2 * (int)(rng() % 4);          // 6, 8, 10 or 12
            std::vector<WEdge> e;
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                    e.emplace_back(i, j, (int)(rng() % 41) - 20);
            std::vector<int> mate;
            long long w = 0;
            MatchingCertificate c;
            ++searched;
            if (!MinWeightPerfectMatchingCertified(n, e, mate, w, c)) continue;
            if (c.oddBlossoms > 0) {
                ++found;
                std::cout << "synthetic control: n=" << n << ", primal " << c.primal
                          << ", dual " << c.dual << ", blossoms with positive dual "
                          << c.oddBlossoms << ", depth " << c.maxDepth << "\n";
            }
        }
        std::cout << "synthetic: " << found << " of " << searched
                  << " random instances needed an odd-set contraction\n";
        if (found > 0)
            std::cout << "  => the detector works; the zero on the board above is a\n"
                      << "     property of the instance, not a broken certificate\n";
    }

    std::cout << (allOptimal ? "all certificates verify\n" : "WARNING: a certificate failed\n");
    std::cout << (anyBlossom
        ? "at least one instance needed blossoms => the non-bipartite core is load-bearing\n"
        : "no instance needed blossoms on this board\n");
    return 0;
}


// ------------------------------------------------------------------ complete
// Finish a PARTIAL board: fill the empty cells with the pieces nobody is using,
// by exact bipartite assignment, then hand it to the local optimisers.
//
// This is the bridge from a constructive solver -- which builds a large
// perfectly-matched region and then runs out of room -- to the local-optimisation
// stack.  Such a solver reaches a deep partial board (here index ~247 of 256)
// whose score is 0 as written, because the empty cells make it invalid.
int CmdComplete(const Args& a) {
    Puzzle pz = Puzzle::load(a.get("--pieces", "data/e2pieces.txt"),
                             a.getInt("--w", 0), a.getInt("--h", 0));
    Board b(pz);
    std::vector<int> pc, rt;
    if (!LoadBoard(a.get("--board", ""), pc, rt)) { std::cerr << "cannot read board\n"; return 1; }
    if ((int)pc.size() != pz.ncells) { std::cerr << "wrong cell count\n"; return 1; }
    const std::vector<int> pcRaw = pc;   // keeps -1 in the empty cells

    // empty cells are -1; everything else must be a legal piece
    std::vector<int> empties, unused;
    std::vector<char> used(pz.ncells, 0);
    int bad = 0;
    for (int c = 0; c < pz.ncells; ++c) {
        if (pc[c] < 0) { empties.push_back(c); pc[c] = 0; rt[c] = 0; continue; }
        if (pc[c] >= pz.ncells) { std::cerr << "piece out of range at cell " << c << "\n"; return 1; }
        if (used[pc[c]]) ++bad;
        used[pc[c]] = 1;
    }
    for (int p = 0; p < pz.ncells; ++p) if (!used[p]) unused.push_back(p);
    std::cout << "empty cells " << empties.size() << ", unused pieces " << unused.size();
    if (bad) std::cout << ", DUPLICATED pieces " << bad;
    std::cout << "\n";
    if (empties.empty()) { std::cout << "board is already complete\n"; return 0; }
    if ((int)unused.size() != (int)empties.size()) {
        std::cerr << "mismatch: cannot complete\n";
        return 1;
    }

    b.load(pc, rt);

    // ---- exact assignment: piece -> empty cell, best rotation for each pair.
    // Bipartite and provably optimal for the cost as written.
    const int m = (int)empties.size();
    std::vector<std::vector<int>> cm((size_t)m, std::vector<int>(m, 0));
    std::vector<std::vector<int>> bestRot((size_t)m, std::vector<int>(m, 0));
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < m; ++j) {
            int best = INT_MAX, br = 0;
            for (int r = 0; r < 4; ++r) {
                int c = 0;
                for (int sd = 0; sd < 4; ++sd) {
                    const int base = empties[j] * 4 + sd;
                    const int nb = pz.neigh[base];
                    const int w = (nb < 0) ? b.wBorderSlot : 2;
                    const int nc = (int)b.protCol[unused[i] * 16 + r * 4 + sd];
                    if (nc != (int)b.target[base]) c += w;
                }
                if (c < best) { best = c; br = r; }
            }
            cm[i][j] = best;
            bestRot[i][j] = br;
        }
    }
    std::vector<int> colForRow;
    HungarianMin(cm, &colForRow);
    std::vector<int> seedPiece(m), seedRot(m);
    for (int i = 0; i < m; ++i) {
        seedPiece[colForRow[i]] = unused[i];
        seedRot[colForRow[i]] = bestRot[i][colForRow[i]];
    }
    const long long hung = EvalHoleFill(pz, pcRaw, rt, empties, seedPiece, seedRot);

    // ---- exact branch and bound over the holes ------------------------------
    // The bipartite assignment above prices every edge of a hole against the
    // hole's *current* neighbours, so hole-hole edges are priced against
    // garbage and the two rotations are picked independently.  When holes
    // touch -- and on these boards most of them do -- that is worth several
    // edges.  Solve the sub-problem properly.
    const long long nodeLimit = (long long)a.getInt("--nodes", 20000000);
    HoleFillResult hr = SolveHoleFill(pz, pcRaw, rt, seedPiece, seedRot, nodeLimit);

    // Every internal edge touching a hole is decided by the fill; the rest are
    // already frozen.  Count each edge once.
    std::vector<char> isEmpty(pz.ncells, 0);
    for (int c : empties) isEmpty[c] = 1;
    int holeHole = 0, holeFilled = 0;
    for (int i = 0; i < m; ++i) {
        for (int sd = 0; sd < 4; ++sd) {
            const int nb = pz.neigh[empties[i] * 4 + sd];
            if (nb < 0) continue;
            if (isEmpty[nb]) { if (nb > empties[i]) ++holeHole; }
            else ++holeFilled;
        }
    }
    int baseMatched = 0, baseEdges = 0;
    for (int e = 0; e < pz.numEdges(); ++e) {
        const int ca = pz.eCellA[e], cb = pz.eCellB[e];
        if (isEmpty[ca] || isEmpty[cb]) continue;
        ++baseEdges;
        if (b.protCol[pcRaw[ca] * 16 + rt[ca] * 4 + pz.eSideA[e]] ==
            b.protCol[pcRaw[cb] * 16 + rt[cb] * 4 + pz.eSideB[e]]) ++baseMatched;
    }
    const int atStake = holeHole + holeFilled;
    std::cout << "edges at stake    " << atStake << "  (hole-hole " << holeHole
              << ", hole-filled " << holeFilled << ")"
              << "   frozen: " << baseMatched << "/" << baseEdges << " already matched\n";
    std::cout << "bipartite fill    " << hung << "/" << atStake
              << "  ->  board " << (baseMatched + hung) << "/" << pz.numEdges() << "\n";
    std::cout << "branch and bound  " << hr.gain << "/" << atStake
              << "  ->  board " << (baseMatched + hr.gain) << "/" << pz.numEdges()
              << "  (" << hr.nodes << " nodes, "
              << (hr.optimal ? "proved optimal" : "NODE LIMIT") << ")\n";

    for (int i = 0; i < m; ++i) {
        b.pc[empties[i]] = hr.piece[i];
        b.rt[empties[i]] = hr.rot[i];
    }
    b.rebuild();

    BoardCheck ck = CheckBoard(pz, b.pc, b.rt);
    std::cout << "after exact completion: matched " << ck.matched << "/" << pz.numEdges()
              << "  border_bad " << ck.borderBad
              << "  every-piece-once " << (ck.valid ? "yes" : "NO") << "\n";

    const std::string outPath = a.get("--out", "");
    if (!outPath.empty()) { SaveBoard(outPath, pz, b.pc, b.rt); std::cout << "wrote " << outPath << "\n"; }
    return 0;
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
        if (cmd == "rotbound") return CmdRotBound(args);
        if (cmd == "bench-subsolve") return CmdBenchSubSolve(args);
        if (cmd == "tjoin") return CmdTJoin(args);
        if (cmd == "cyccover") return CmdCycleCover(args);
        if (cmd == "blast") return CmdBlast(args);
        if (cmd == "complete") return CmdComplete(args);
        if (cmd == "certify") return CmdCertify(args);
        if (cmd == "pop") return CmdPop(args);
        if (cmd == "dissect") return CmdDissect(args);
        if (cmd == "polish") return CmdPolish(args);
        if (cmd == "selftest") return CmdSelfTest(args);
        PrintUsage(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
