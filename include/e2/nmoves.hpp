// Eternity-Blossom -- the remaining non-bipartite moves.
//
// Both of these reduce to minimum-weight PERFECT matching on a graph that
// contains odd cycles, so both need Edmonds' algorithm rather than an
// assignment solver.  They are deliberately different from each other and from
// the T-join in tjoin.hpp:
//
//   CycleCoverPass  -- a 2-FACTOR.  Every chosen cell ends up on a cycle, and
//                      the pieces rotate one step around it.  Found by Tutte's
//                      gadget: replace each vertex by two copies and each edge
//                      by a K2,2; a perfect matching there is a perfect
//                      2-matching back in the original graph.  Because the cell
//                      graph is complete (any piece may move to any cell), it
//                      contains triangles, the gadget contains triangles, and
//                      the instance is genuinely non-bipartite.
//
//   BlastPass       -- matching-guided ruin & recreate.  A maximum-weight
//                      matching pairs the unhappy cells up; the ruin set is the
//                      union of shortest paths between each pair (so the damage
//                      is connected rather than scattered); the pieces in that
//                      set are then reassigned to its cells EXACTLY, by
//                      Hungarian, each in its best rotation.  The matching that
//                      chooses the ruin set is non-bipartite; the repair is
//                      bipartite and exact.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"

namespace e2 {

// ---------------------------------------------------------------- cycle cover

struct CycleCoverParams {
    // Which cells to cover:
    //   unhappy       the cells touching a mismatched edge (default)
    //   unhappy-hood  those plus their grid neighbours
    //   sample        a random sample of the board
    //   all           the whole board
    std::string set = "unhappy";
    int maxCells = 32;          // gadget has 2*|S| vertices and 4*C(|S|,2) edges
    int maxCycles = 24;
    int maxCycleLen = 32;
    bool acceptOnlyImproving = true;
    bool verbose = false;
};

struct CycleCoverResult {
    bool ran = false, improved = false;
    int cells = 0;              // |S| fed to the 2-factor
    int cycles = 0;             // components found (including 2-cycles)
    int applied = 0;            // rotations actually kept
    int movedCells = 0;
    long long costBefore = 0, costAfter = 0, actualGain = 0;
    double seconds = 0.0;
    std::string error;
};

CycleCoverResult CycleCoverPass(Board& b, const CycleCoverParams& pr,
                                MatchingStats* ms = nullptr);

// The 2-factor itself, exposed for testing: `cycles` covers every cell of
// `cells` exactly once.  Uses Tutte's gadget, so the matching is non-bipartite
// whenever `cells` has three mutually adjacent members.
bool PerfectTwoMatching(const Puzzle& pz, const std::vector<int>& cells,
                        const std::vector<std::vector<int>>& w,
                        std::vector<std::vector<int>>& cycles,
                        MatchingStats* ms = nullptr);

// --------------------------------------------------- matching-guided LNS

struct BlastParams {
    int maxPairs = 12;          // matching pairs => up to 2*maxPairs terminals
    int maxCells = 60;          // cap on the size of the ruin set
    int maxPaths = 16;
    int maxPathLen = 16;
    bool acceptOnlyImproving = true;
    bool verbose = false;
};

struct BlastResult {
    bool ran = false, improved = false;
    int terminals = 0;          // unhappy cells paired by the matching
    int paths = 0;
    int ruined = 0;             // cells in the ruin set
    long long predictedGain = 0;  // what the assignment promised
    long long actualGain = 0;     // what the board delivered
    long long costBefore = 0, costAfter = 0;
    double seconds = 0.0;
    std::string error;
};

// One ruin & recreate round: matching picks the ruin set, Hungarian repairs it.
BlastResult BlastPass(Board& b, const BlastParams& pr, MatchingStats* ms = nullptr);

}  // namespace e2
