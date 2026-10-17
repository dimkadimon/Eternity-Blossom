// Eternity-Blossom -- correctness checks for the coarse, rotation and sub-solve
// modules.  SPDX-License-Identifier: MIT
//
// Every operator here claims to be EXACT: the block deltas, the strip DP, the
// sub-board branch and bound, and the rotation upper bound.  Each claim is
// checked against brute force on instances small enough to enumerate, because a
// move that is nearly right is worse than no move at all -- it silently
// corrupts the search's belief about the objective.

#pragma once

#include <cstdint>

#include "e2/board.hpp"

namespace e2 {

// Coarse moves: block-rotation and block-swap deltas against the real cost
// change, the invariant that a rigid block motion preserves the block's
// internal edges, and the validity of the domino dissections.
bool SelfTestCoarse(const Puzzle& pz, int trials, uint64_t seed);

// Strip DP against exhaustive enumeration, and the rotation upper bound against
// both an exhaustive optimum (small board) and a large sample of random
// rotation assignments (the real board).
bool SelfTestRotation(const Puzzle& pz, int trials, uint64_t seed);

// Sub-board branch and bound against exhaustive enumeration, with and without
// the stub bound, to confirm the bound prunes work without pruning the optimum.
bool SelfTestSubSolve(const Puzzle& pz, int trials, uint64_t seed);

// Dissection reassignment.  The whole claim of dissect.hpp is that the
// independence of the chosen set makes the cost ADDITIVE, so the Hungarian
// optimum is the true optimum -- which means the improvement the solver
// predicts must equal, exactly, the improvement measured on the real board.
// Any discrepancy at all is a bug: either a group's internal edges moved, or
// two selected groups were allowed to touch.
bool SelfTestDissect(const Puzzle& pz, int trials, uint64_t seed);

}  // namespace e2
