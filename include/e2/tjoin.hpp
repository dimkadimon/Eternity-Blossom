// Eternity-Blossom -- T-join moves.
//
// A T-join is the classic answer to "connect up these vertices as cheaply as
// possible": given a graph and an even-size set T of vertices, find a
// minimum-weight set of edges whose odd-degree vertices are exactly T.  It
// reduces to minimum-weight perfect matching on the complete graph over T with
// shortest-path distances as weights -- NON-BIPARTITE, so Blossom, and a
// structurally different use of the matching machinery from anything else here.
//
// How it becomes a move.  Let T be the set of unhappy cells (those touching a
// mismatched edge).  A T-join connects them up with short paths through the
// board.  Each path of the join is a chain of cells; rotate the pieces one step
// along the chain, so every cell on it -- every unhappy cell in particular --
// receives a different piece.
//
// This is not expressible as any product of disjoint swaps, which is the point:
// a cyclic shift of k pieces is one k-cycle, and the swap operators here can
// only ever build 2-cycles.  It explores a part of the permutation group that
// everything else in this repository is blind to.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"

namespace e2 {

struct TJoinParams {
    int maxCells = 40;        // cap on |T|; the matching is over a complete graph
    int maxPaths = 24;        // cap on cyclic shifts applied in one pass
    int maxPathLen = 24;      // ignore paths longer than this
    bool acceptOnlyImproving = true;
    bool verbose = false;
};

struct TJoinResult {
    bool ran = false, improved = false;
    int unhappy = 0;          // |T| fed to the matching
    int paths = 0;            // chains in the join
    int shifts = 0;           // cyclic shifts applied
    int movedCells = 0;
    long long costBefore = 0, costAfter = 0, actualGain = 0;
    long long matchingEdges = 0;
    double seconds = 0.0;
    std::string error;
};

// One T-join pass: build T from the unhappy cells, solve the T-join with
// Blossom, and cyclically shift the pieces along each chain of the join.
TJoinResult TJoinPass(Board& b, const TJoinParams& pr, MatchingStats* ms = nullptr);

// The chains themselves, exposed for testing: `chains` is a list of cell
// sequences, each meant to be rotated one step forward.
bool TJoinChains(const Puzzle& pz, const std::vector<int>& terminals,
                 std::vector<std::vector<int>>& chains, MatchingStats* ms = nullptr);

}  // namespace e2
