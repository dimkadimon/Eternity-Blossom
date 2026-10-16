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
#include "e2/operators.hpp"

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
    RegionParams region;

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
SearchOutcome SearchParallel(const Puzzle& pz, const SearchParams& params, int threads);

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
