// Eternity-Blossom -- rotation-only optimisation, and an upper bound for it.
//
// SPDX-License-Identifier: MIT
//
// ROTATION-ONLY SUB-PROBLEM
// -------------------------
// Freeze the POSITION of every piece (which piece sits on which cell) and let
// only the ROTATIONS vary.  What is left is a pairwise CSP on the 16x16 grid:
// 256 variables of domain 4, one constraint per internal edge saying
//
//     colour shown by piece P_i toward j  ==  colour shown by piece P_j toward i
//
// which, because a rotation cycles a piece's four colours past a given side, is
// the constraint "the colour P_i shows here is one of P_j's four colours".  The
// constraint graph is the grid, which is not a tree, so the problem is NP-hard
// in general -- but it has a great deal of structure and two things can be done
// with it.
//
// (1) EXACT OPTIMISATION OF A STRIP (a move).
//     Take h consecutive rows.  The sub-problem restricted to those rows has
//     pathwidth h, so it can be solved EXACTLY by a DP that sweeps the columns
//     carrying the 4^h rotations of the current column as its state.  h = 2 is
//     16 states, h = 3 is 64, h = 4 is 256 -- all cheap.  The result is a move
//     that re-solves 32 (or 48, or 64) rotation variables simultaneously and
//     optimally, which is enormously stronger than the usual "pick a cell, try
//     its four rotations, keep the best" neighbourhood.  It never worsens the
//     board, because the current rotations are one of the DP's feasible states.
//
// (2) AN UPPER BOUND (an oracle).
//     Partition the 480 internal edges into groups and solve each group
//     exactly; the sum of the group optima is an upper bound on what ANY
//     rotation assignment can achieve, because it relaxes the constraints
//     BETWEEN groups.  With strips of h rows:
//
//        * every strip contributes the exact optimum over its own internal
//          edges (the DP above, told to ignore the edges leaving the strip), and
//
//        * the seams between consecutive strips contribute their exact optimum
//          too, and for free: the seam edges are vertex-disjoint, so they do not
//          interact at all and the optimum is the sum of the per-edge optima.
//
//     Those two families partition all 480 edges, so
//
//        matched  <=  480 - (sum of strip minima + sum of seam minima)
//
//     is a rigorous upper bound on the number of matched edges reachable from
//     THIS position assignment by rotating alone.  If it is below 480 the
//     assignment can be discarded outright: no amount of rotation work on it can
//     produce a solution.  Inside a search that is a diversification trigger --
//     stop polishing the rotations and go and move some pieces.
//
//     Two weaker bounds are provided for comparison: the per-edge bound (an edge
//     that can never match under any rotation of both endpoints) and the star
//     bound (optimise each cell together with its four neighbours, which is the
//     natural "look at one tile and its neighbourhood" relaxation; note that it
//     counts every internal edge twice, so it must be halved before being
//     compared with 480).

#pragma once

#include <vector>

#include "e2/board.hpp"

namespace e2 {

// h consecutive rows (columns=false) or columns (columns=true), starting at
// `start`, spanning the whole board along the other axis.
struct StripSpec {
    bool columns = false;
    int start = 0;
    int k = 2;  // height; limited to 4 by the DP (4^4 = 256 states)
};

struct StripOpt {
    long long optimum = 0;   // best value found by the DP
    long long current = 0;   // value of the present rotations, same terms
    std::vector<int> cells;  // the strip's cells
    std::vector<int> rot;    // optimal rotation for each cell (parallel to cells)
};

// Exact optimum over the strip's rotations.
//   moveMode = true  : counts every edge incident to the strip (internal weight
//                      2, border weight wBorderSlot) -- the result is a move.
//   moveMode = false : counts only edges strictly inside the strip (weight 1,
//                      border weight 0) -- the result feeds the upper bound.
StripOpt OptimizeStrip(const Board& b, const StripSpec& sp, bool moveMode);

// Write o.rot onto o.cells and rebuild.  delta = o.optimum - o.current.
void ApplyStripOpt(Board& b, const StripOpt& o);

struct StripSweepResult {
    int strips = 0;
    int applied = 0;
    long long gain = 0;  // change of Board::cost (negative is good)
    double seconds = 0.0;
};

// Re-solve every strip of height `k` in turn (row strips and, optionally,
// column strips), keeping each improvement.
StripSweepResult StripRotateSweep(Board& b, int k, bool columns);

// ------------------------------------------------------------------- the oracle
struct RotationBoundResult {
    long long mismatchLB = 0;  // lower bound on the number of mismatched edges
    long long matchedUB = 0;   // = numEdges - mismatchLB
    int strips = 0;
    long long seamPart = 0;
    long long stripPart = 0;
    double seconds = 0.0;
};

RotationBoundResult RotationUpperBound(const Board& b, int k, bool columns);

// Per-edge bound: an edge that can never match under any rotation of both of
// its pieces.  Returns the upper bound on matched edges.
long long EdgeBoundUpper(const Board& b, long long* impossible = nullptr);

// Star bound: for each cell, the best it can do jointly with its neighbours,
// ignoring everything else.  Returns the upper bound on matched edges (the raw
// sum counts every edge twice and is written to `rawSum`).
long long StarBoundUpper(const Board& b, long long* rawSum = nullptr);

}  // namespace e2
