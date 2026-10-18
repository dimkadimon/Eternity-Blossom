// Eternity-Blossom -- a memetic (population + crossover) layer.
//
// Why this exists.  Every exact operator in this repository -- Hungarian
// reassignment, Blossom swap packets, the rotation strip DP, the sub-board
// branch and bound -- converges, and once it has converged it reports zero
// available gain.  Running any of them for ten times as long does not help:
// 900 s scores 438 while 300 s scores 441.  The operators are not the
// bottleneck; the single trajectory they sit inside is.
//
// The published 467-470 Eternity II results all use population search with
// crossover.  This file adds exactly that, reusing SearchOnce as the local
// improver so every existing operator keeps working inside it.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "e2/puzzle.hpp"
#include "e2/search.hpp"

namespace e2 {

struct PopulationParams {
    uint64_t seed = 1;
    double timeLimitSeconds = 300.0;

    int popSize = 12;              // number of boards kept alive
    double initSeconds = 12.0;     // local search per individual at start
    double breedSeconds = 6.0;     // local search per child, per generation
    int elite = 2;                 // individuals carried over untouched
    int tournament = 3;            // parents chosen from this many candidates

    // Crossover transplants a rectangle this size (side chosen uniformly in
    // [regionMin, regionMax]) from one parent into the other.
    int regionMin = 3;
    int regionMax = 9;

    // Local search temperature for the two phases.  They are NOT the same job.
    // Seeding explores, so it wants the hot schedule the solver was tuned on.
    // Breeding has to REPAIR a child while keeping the structure it inherited
    // from two good parents -- running that at t0 = 3.0 accepts almost anything
    // and throws the inheritance away, which is exactly what made the first
    // version of this no better than plain restarts.
    double breedT0 = 0.30;
    double breedT1 = 0.05;

    bool useCrossover = true;      // false => pure independent restarts
    // Place the pieces displaced by the transplant with an exact bipartite
    // assignment (Hungarian) instead of dumping them into the freed cells in
    // arbitrary order.  Dumping them is the destructive part of the crossover:
    // it wrecks |R| scattered cells that had nothing to do with the transplant,
    // and that is what left children ~35 cost units worse than their parents.
    bool repairAssign = true;
    bool verbose = false;
    bool trace = false;            // print (seconds, best, mean) per generation

    // Boards to START the population from instead of random restarts.
    // seedPc[i]/seedRt[i] describe board i; the first min(popSize, size)
    // individuals ARE those boards, unchanged, and any slots left over are
    // filled by independent runs as before.  This is what lets the hybrid
    // pipeline's boards be crossed with each other, which is the one thing
    // `pop` could not do: on its own it can only cross the offspring of its
    // own random restarts, and those are ~20 edges worse than a DFS board.
    std::vector<std::vector<int>> seedPc, seedRt;

    // passed through to SearchOnce for every local search
    SearchParams search;
};

struct PopulationStats {
    int generations = 0;
    int crossovers = 0;
    int improvements = 0;          // children that beat the individual they replaced
    long long bestCost = 0;
    long long children = 0;              // crossover children produced
    long long childCostSum = 0;          // their cost BEFORE local search
    long long childCostMin = (1LL << 60);
    double initSeconds = 0.0;
    double breedSeconds = 0.0;
    double seconds = 0.0;
};

struct PopulationOutcome {
    std::vector<int> pc, rt;
    long long cost = 0;
    long long score = 0;
    long long borderBad = 0;
    PopulationStats stats;
};

PopulationOutcome PopulationSearch(const Puzzle& pz, const PopulationParams& params);

// Crossover: transplant the rectangle [r0,r0+h) x [c0,c0+w) of parent A into a
// copy of parent B.  Returns false if the repair could not be made consistent.
//
// The repair is the interesting part.  After the transplant, the pieces that
// were in the rectangle under B but are not in it under A are unplaced, and the
// pieces that are new to the rectangle are now duplicated at their old
// positions elsewhere in B.  Those two sets are always the same size, so the
// displaced pieces slot into the freed cells and every piece is used exactly
// once.  Rotations travel with their piece.
bool CrossoverRegion(const Puzzle& pz,
                     const std::vector<int>& pcA, const std::vector<int>& rtA,
                     const std::vector<int>& pcB, const std::vector<int>& rtB,
                     int r0, int c0, int h, int w,
                     std::vector<int>& outPc, std::vector<int>& outRt,
                     const Board* ctx = nullptr, bool repairAssign = true);

}  // namespace e2
