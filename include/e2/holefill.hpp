// Eternity-Blossom -- exact completion of a PARTIAL board (hole filling).
//
// SPDX-License-Identifier: MIT
//
// WHY THE BIPARTITE COMPLETION IS NOT ENOUGH
// ------------------------------------------
// `complete` fills the empty cells ("holes") by minimum-weight bipartite
// assignment of the unused pieces to the holes.  That is optimal *for the cost
// matrix as written*, and the matrix is
//
//      cost[piece][hole] = min over the 4 rotations of
//                          (mismatched edges against the hole's neighbours)
//
// which is only the true cost when every neighbour of every hole is already
// filled.  When two holes touch, each one prices the shared edge against
// whatever placeholder the other still holds, so
//
//      * the edge is priced against garbage, and
//      * the two rotations are chosen independently, so the assignment cannot
//        even in principle decide to make that edge match.
//
// On the boards the constructive DFS hands us the holes are a tight cluster --
// which is good, it minimises the number of edges at stake -- but it means most
// of the edges at stake are exactly the hole-hole ones the matrix misprices.
//
// WHAT THIS MODULE DOES
// ---------------------
// It solves the completion sub-problem EXACTLY: choose which unused piece goes
// to which hole, and with which rotation, maximising the number of matched
// edges over every edge touching a hole (hole-hole edges included) subject to
// the border staying grey.  Branch and bound over the holes in an order that
// keeps the frontier small, with the admissible bound
//
//      gain + sum over unplaced holes of (best fixed-neighbour gain still
//                                         available to it)
//           + (hole-hole edges not decided yet)
//
// Both extra terms over-count (pieces are shared between holes, and an
// undecided edge is worth at most +1), so the bound never cuts off the optimum.
// For the 6-9 hole boards coming out of the DFS this is a few thousand nodes.

#pragma once

#include <vector>

#include "e2/puzzle.hpp"

namespace e2 {

struct HoleFillResult {
    int holes = 0;
    long long gain = 0;    // matched edges on the edges touching a hole
    long long nodes = 0;
    bool optimal = false;  // proved optimal (node limit not reached)
    std::vector<int> cell;   // the hole cells
    std::vector<int> piece;  // piece chosen for each hole (parallel to cell)
    std::vector<int> rot;    // rotation chosen for each hole
};

// Value of one concrete fill: matched edges over every edge touching a hole,
// minus a large penalty for each border slot that is not grey.  `cells`,
// `pieces` and `rots` are parallel; pc/rt describe the rest of the board (the
// hole entries are overwritten).
long long EvalHoleFill(const Puzzle& pz, const std::vector<int>& pc, const std::vector<int>& rt,
                       const std::vector<int>& cells, const std::vector<int>& pieces,
                       const std::vector<int>& rots);

// Branch and bound.  `seedPiece`/`seedRot` are parallel to the empty cells in
// increasing cell order and give a starting solution (typically the bipartite
// one); the search only ever improves on it.  `nodeLimit` caps the work; the
// best solution found so far is returned either way, with `optimal` saying
// whether the search finished.
HoleFillResult SolveHoleFill(const Puzzle& pz, const std::vector<int>& pc,
                             const std::vector<int>& rt,
                             const std::vector<int>& seedPiece = std::vector<int>(),
                             const std::vector<int>& seedRot = std::vector<int>(),
                             long long nodeLimit = 20000000);

}  // namespace e2
