// Eternity-Blossom -- exact bipartite assignment (Hungarian algorithm).
//
// SPDX-License-Identifier: MIT
//
// Some of the operators in this project are genuinely non-bipartite and need
// Blossom VI.  Others are plain assignment problems: n items, n slots, a cost
// for each (item, slot) pair, find the bijection minimising the total.  Feeding
// those to a general matching solver works but is wasteful -- a Blossom VI call
// on a few hundred vertices costs ~1 ms because it builds the whole primal-dual
// machinery including blossom contraction that a bipartite instance can never
// use.  An O(n^3) Hungarian on the same size costs tens of microseconds.
//
// That factor matters a lot here.  The dissection operator (dissect.hpp) wants
// to solve thousands of assignment problems per second, and it can only do that
// with a real Hungarian.  Using the blossom engine for it would be a 100x
// slowdown for no benefit -- and it would obscure which operators actually need
// odd-cycle contraction and which do not.
//
// This is the standard shortest-augmenting-path (Jonker-Volgenant style)
// algorithm, O(n^3) with small constants, for square cost matrices.

#pragma once

#include <vector>

namespace e2 {

// Minimum-cost perfect assignment.  `outColForRow[i]` is the column assigned to
// row i.  Returns the total cost (as long long, so overflow is not a worry for
// the sizes used here).
long long HungarianMin(const std::vector<std::vector<int>>& cost,
                       std::vector<int>* outColForRow = nullptr);

}  // namespace e2
