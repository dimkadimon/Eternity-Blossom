// Eternity-Blossom -- the search that drives everything.
//
// SPDX-License-Identifier: MIT
//
// Eternity II is NP-hard (edge matching with all-distinct pieces is NP-complete),
// so no polynomial-time algorithm -- blossom or otherwise -- can solve it.  What
// a matching solver *can* do is supply operators that are optimal in polynomial
// time for sub-problems that a plain local search gets wrong.  This file wires
// them into a Late-Acceptance Hill-Climbing (LAHC) search:
//
//   * the raw move is "swap the pieces on two cells, re-optimising both
//     rotations", evaluated incrementally (Board::deltaSwap);
//   * every `blossomEvery` iterations, when the search is stuck, we hand the
//     whole conflict frontier to Blossom VI and let it compute the provably
//     optimal packet of disjoint swaps (operators.hpp (1));
//   * optionally, a region is re-solved exactly with branch & bound, pruned by
//     the stub-matching lower bound (operators.hpp (2)/(3)).
//
// The matching operators are *proposers*: the true objective change is measured
// on the real board and a packet is rolled back if it does not help, so the
// search remains exact even though the matching optimises a surrogate.

#pragma once

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"
#include "e2/coarse.hpp"
#include "e2/operators.hpp"
#include "e2/rotation.hpp"
#include "e2/subsolve.hpp"
#include "e2/nmoves.hpp"

namespace e2 {

enum class InitMode { StructuredRandom, Random, Greedy, GreedyStructured };
enum class AcceptMode { LAHC, SA };

struct SearchParams {
    uint64_t seed = 1;
    double timeLimitSeconds = 60.0;
    InitMode init = InitMode::GreedyStructured;

    AcceptMode accept = AcceptMode::LAHC;
    // simulated annealing (used when accept == SA).  Costs are measured in
    // "slot" units: one mismatched internal edge is 2, one bad border slot is
    // 2*wBorder.
    double saT0 = 4.0;
    double saT1 = 0.15;

    // LAHC
    int lahcLength = 500;          // history length
    double pConflictFirst = 0.85;  // chance the first cell of a swap is a conflicted one
    double pRotate = 0.15;         // chance of a pure rotation move instead of a swap
    double pTargeted = 0.85;       // chance of an "aimed" move (violated edge + matching colour)
    double pCycle3 = 0.35;         // chance that an aimed move is a 3-cycle instead of a swap
    int sampleK = 1;               // proposals evaluated per move (steepest-descent = large K)
    int stallLimit = 300000;       // iterations without improvement before perturbing
    int perturbSwaps = 24;         // random swaps used as a perturbation

    // ---- Iterated local search semantics of the perturbation.
    // The search holds an incumbent (`best`) and a current state that drifts
    // away from it, because LAHC and SA both accept worsening moves.  Kicking
    // the DRIFTED state throws away a fresh incumbent and restarts from a
    // board that may be far worse than the best one found, which is not what
    // ILS does -- it kicks the incumbent.  On by default; --no-perturb-best
    // restores the old behaviour for comparison.
    bool perturbFromBest = true;
    // Grow the kick while it keeps failing and shrink it when it lands, so the
    // search spends its time at the useful kick size instead of at one fixed
    // guess.  (A fixed size is either too small to escape or so large it is a
    // random restart.)
    bool adaptivePerturb = true;
    int perturbMax = 160;          // ceiling on swaps in a single kick
    double perturbGrowth = 0.60;   // strength added per consecutive failure
    double perturbShrink = 0.70;   // strength multiplied by this on success

    // Blossom swap packet
    bool useBlossomSwaps = true;
    int blossomCheckEvery = 20000;   // consider a blossom pass every N iterations
    int blossomStallTrigger = 4000;  // ...if no improvement for this many iterations
    BlossomSwapParams blossom;

    // Optimal reassignment of a set of cells (matching over cells x pieces)
    bool useAssign = true;
    int assignEvery = 200000;    // iterations between attempts
    int assignCells = 64;        // cells per pass
    AssignParams assign;

    // Exact region re-optimisation (branch & bound + stub bound)
    bool useRegionLNS = false;
    int regionEvery = 200000;
    int regionSize = 8;
    // Grow each region from a cell that touches a violated slot rather than
    // from a uniformly random one.  On a converged board almost all the damage
    // sits in a single cluster of unhappy cells, so a uniform seed spends most
    // passes proving that perfect regions are perfect.
    bool regionTargeted = false;
    RegionParams region;

    // ---- Coarse-grained moves: rigid 2x2 / 4x4 / 8x8 blocks, and domino
    // dissections.  A block rotation preserves how many of the block's internal
    // edges match, so it only re-exposes the boundary; a block swap preserves
    // both blocks' internals.  Both are large moves the fine-grained swap
    // neighbourhood cannot reach.
    bool useCoarseRotate = true;
    int coarseRotateEvery = 10000;   // iterations between rotate sweeps
    std::vector<int> coarseSides{2}; // square block sizes to use
    bool useCoarseSwap = true;       // Blossom packet of block swaps
    int coarseSwapEvery = 50000;
    CoarseSwapParams coarseSwap;
    int coarsePerturb = 0;           // random block rotations/flips per perturbation
    int coarseDominoes = 0;          // random domino dissections to keep around
    int coarseDominoFlips = 20000;   // mixing steps used to build each one

    // ---- Exact rotation optimisation by strip DP.  Re-solves 32/48/64
    // rotation variables at once, optimally.
    bool useRotStrip = true;
    int rotStripEvery = 20000;
    int rotStripK = 2;               // strip height, 2..4 (4^4 = 256 DP states)
    bool rotStripColumns = true;     // sweep column strips as well as row strips

    // ---- Rotation-only upper bound.  Never changes the board: it answers "can
    // this POSITION assignment still reach a perfect board by rotating alone?",
    // and triggers a perturbation of the positions when the answer is no.
    bool useRotBound = false;
    int rotBoundEvery = 200000;
    int rotBoundK = 3;

    // ---- Exact sub-board branch and bound (pruned by the stub matching bound).
    bool useSubSolve = false;
    int subSolveEvery = 100000;
    int subSolveH = 2, subSolveW = 3;
    SubSolveParams subSolve;

    // ---- Post-kick repair with the 2-factor / ruin-and-recreate moves.
    // These are worth a lot on a board that is NOT converged (from random,
    // cyccover is +530 and blast +382) and exactly zero on one that is, and a
    // freshly kicked board is precisely the first case.  So run them right
    // after a kick, where they can climb back far faster than swap-by-swap SA.
    bool useNMoveRepair = false;
    int nmoveRounds = 3;           // rounds of each operator per kick
    int nmoveMaxCells = 48;        // cell cap handed to the two operators
    CycleCoverParams cyccover;
    BlastParams blast;

    int wBorder = 8;
    bool verbose = false;
    bool trace = false;   // print (seconds, score) as the search progresses
};

struct SearchStats {
    long long iterations = 0;
    long long restarts = 0;
    long long blossomPasses = 0;
    long long blossomImproved = 0;
    long long blossomReverted = 0;
    int blossomTotalPredicted = 0;
    int blossomTotalActual = 0;
    long long assignPasses = 0;
    long long assignImproved = 0;
    int assignTotalActual = 0;
    long long regionRuns = 0;
    long long regionImproved = 0;
    long long regionNodes = 0;
    long long regionBoundCalls = 0;
    long long regionBoundPrunes = 0;

    long long coarseRotateSweeps = 0;
    long long coarseRotates = 0;
    long long coarseRotateGain = 0;
    long long coarseSwapPasses = 0;
    long long coarseSwapImproved = 0;
    long long coarseSwapPredicted = 0;
    long long coarseSwapActual = 0;
    long long coarseSwapPairs = 0;

    long long rotStripSweeps = 0;
    long long rotStripApplied = 0;
    long long rotStripGain = 0;

    long long rotBoundCalls = 0;
    long long rotBoundTriggers = 0;   // times the bound killed the assignment
    long long rotBoundBest = 1 << 30; // smallest upper bound on matched edges seen

    long long nmoveRounds = 0;
    long long blastImproved = 0;
    long long cycImproved = 0;

    long long subSolveRuns = 0;
    long long subSolveImproved = 0;
    long long subSolveExact = 0;
    long long subSolveNodes = 0;
    long long subSolveStubCalls = 0;
    long long subSolveStubPrunes = 0;
    double seconds = 0.0;
    MatchingStats matching;
};

struct SearchOutcome {
    std::vector<int> pc, rt;
    long long cost = 0;
    long long score = 0;      // matched internal edges
    long long borderBad = 0;
    SearchStats stats;
};

// One independent run.  Respects params.timeLimitSeconds.
SearchOutcome SearchOnce(const Puzzle& pz, const SearchParams& params,
                         const std::vector<int>* startPc = nullptr,
                         const std::vector<int>* startRt = nullptr);

// Run `threads` independent searches and keep the best board found.
// If a start board is supplied every thread begins from it (with a different
// seed, so the trajectories differ at once) -- that turns `solve` into basin
// hopping around a known-good board, which is the right way to push past a
// record rather than restarting from scratch every time.
SearchOutcome SearchParallel(const Puzzle& pz, const SearchParams& params, int threads,
                             const std::vector<int>* startPc = nullptr,
                             const std::vector<int>* startRt = nullptr);

// Recompute everything from scratch (used to validate the incremental scoring).
struct BoardCheck {
    long long matched = 0;
    long long mismatched = 0;
    long long borderBad = 0;
    long long duplicatePieces = 0;   // 0 means every piece used exactly once
    bool valid = false;
};
BoardCheck CheckBoard(const Puzzle& pz, const std::vector<int>& pc, const std::vector<int>& rt);

// Serialise / parse a board: one "<piece> <rotation>" per cell, row-major.
bool SaveBoard(const std::string& path, const Puzzle& pz, const std::vector<int>& pc,
               const std::vector<int>& rt, const std::string& comment = "");
bool LoadBoard(const std::string& path, std::vector<int>& pc, std::vector<int>& rt,
               std::string* comment = nullptr);

}  // namespace e2
