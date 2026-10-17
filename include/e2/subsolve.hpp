// Eternity-Blossom -- exact branch-and-bound over a sub-board.
//
// SPDX-License-Identifier: MIT
//
// Take a region R of the board and a pool of pieces, and find the assignment of
// pieces to cells AND rotations that minimises the number of violated slots on
// R's internal edges and on R's boundary -- exactly.  The region's surroundings
// are frozen, so the boundary edges are unary costs, not couplings.
//
// This is the third and strongest LNS operator: instead of perturbing a region
// at random and hoping, we re-solve the region to optimality.  The question is
// only how large a region can be proved optimal, and that is entirely a
// question of how good the lower bound is.
//
// THREE LOWER BOUNDS, in increasing order of strength and cost
// -----------------------------------------------------------
// All of them are expressed in the same units as Board::cost divided by two:
// one unit per mismatched internal edge, wBorderRel units per bad border slot.
//
// (1) EDGE BOUND.  Maintain a histogram of the colours present in the remaining
//     pool.  Any not-yet-decided slot whose required colour is absent from every
//     remaining piece is certainly violated.  O(|R|) per node, and it is the only
//     bound cheap enough to run at every node.
//
// (2) ASSIGNMENT BOUND.  For each remaining cell c and remaining piece p let
//     v(c,p) be the smallest number of c's already-decided slots that p can
//     violate, minimised over rotations.  Whatever the eventual assignment sigma
//     is, the remaining cost is at least sum_c v(c, sigma(c)), and that is at
//     least the minimum over ALL injective sigma -- a rectangular assignment
//     problem, solved here by an O(m^3) Hungarian.  Surplus pool pieces are
//     absorbed by dummy rows of cost zero, which keeps the bound valid when the
//     pool is larger than the region.  (Bipartite, so no blossoms needed.)
//
// (3) STUB BOUND -- the non-bipartite one.  Give every remaining piece four
//     stubs, one per side, and every decided slot a demand vertex carrying the
//     colour it requires; charge 1 whenever a matched pair disagrees.  Because a
//     piece may be rotated freely a stub carries no direction, so there is no
//     N/S versus E/W typing, three mutually compatible pieces form a triangle,
//     and the minimum-weight perfect matching is genuinely non-bipartite --
//     this is where Blossom VI earns its place.  Unlike the two bounds above it
//     sees the interaction between the slots of a single piece (a piece with one
//     red edge can only serve one red demand), which is exactly what (1) and (2)
//     over-optimistically ignore.
//
//     It costs a Blossom VI call, which at ~1 ms is three orders of magnitude
//     more than the other bounds, so it is budgeted: only for the last few cells
//     of the region and only up to a fixed number of calls per solve.  This is
//     the regime the bound was designed for -- at the ROOT of the whole board it
//     is 0 and useless (measured), but deep in a branch, with most slots already
//     decided, it is a real constraint.

#pragma once

#include <string>
#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"

namespace e2 {

// Cells of the h x w rectangle at (r0,c0), in row-major order (each cell has
// its west and north neighbours earlier in the order, which is what the DFS
// wants).
std::vector<int> RectRegion(const Puzzle& pz, int r0, int c0, int h, int w);

struct SubSolveParams {
    long long maxNodes = 2000000;
    bool useEdgeBound = true;
    bool useAssignBound = true;
    bool useStubBound = true;
    int assignMaxCells = 12;      // Hungarian is O(m^3); skip it below that for large m
    int stubMaxCells = 9;         // Blossom costs a call; only for <= this many free cells
    int stubMinCells = 0;         // ...and >= this many.
    bool stubAutoShallow = true;  // Override stubMinCells with (cells - 2): the
                                  // bound is only worth its cost where a prune
                                  // still kills a large subtree -- see
                                  // experiments/RESULTS.md, "where to call it".
                                  // ...and >= this many.  A prune at depth d kills the
                                  // whole subtree below it, so calling the expensive
                                  // bound near the leaves (where almost nothing is
                                  // left to kill) wastes it: set this to prune only
                                  // high in the tree, where one call pays for itself.
    long long maxStubCalls = 5000;
    double timeLimitSeconds = 2.0;
    bool requireImprovement = true;  // keep the result only if Board::cost drops
    bool keepEqual = false;          // allow sideways moves
    bool proveOptimal = false;       // start from +inf: search for the true
                                     // optimum rather than merely an improvement
};

struct SubSolveResult {
    bool ran = false;
    bool exact = false;       // the DFS ran to completion: `best` is provably optimal
    bool improved = false;    // the board was actually changed for the better
    int cells = 0, poolSize = 0;
    long long regionBefore = 0, regionAfter = 0;  // in cost/2 units (see above)
    long long realBefore = 0, realAfter = 0;      // Board::cost
    long long nodes = 0;
    long long edgePrunes = 0, assignPrunes = 0, stubPrunes = 0;
    long long stubCalls = 0, assignCalls = 0;
    bool infeasiblePrune = false;
    bool timedOut = false, nodeLimited = false;
    double seconds = 0.0;
};

// Exactly re-solve `cells` using `pool`.  |pool| must be >= |cells|; the stub
// bound is only available (and is only used) when they are equal, because the
// stub model needs one piece per free cell.
SubSolveResult SubSolveRegion(Board& b, const std::vector<int>& cells,
                              const std::vector<int>& pool, const SubSolveParams& pr,
                              MatchingStats* ms = nullptr);

}  // namespace e2
