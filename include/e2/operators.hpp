// Eternity-Blossom -- matching-based operators for Eternity II.
//
// SPDX-License-Identifier: MIT
//
// Two uses of minimum-weight perfect matching, both of which are genuinely
// NON-BIPARTITE and therefore need a real blossom implementation:
//
// (1) BlossomSwapPass -- "optimal packet of disjoint swaps".
//     Think of the current board.  A swap of the pieces sitting on two cells
//     (with both pieces re-oriented) changes the objective by an amount that can
//     be computed exactly.  A *set* of pairwise swaps on pairwise disjoint cells
//     changes the objective by (approximately) the sum of the individual gains,
//     so choosing the best possible packet of disjoint swaps is a MAXIMUM
//     WEIGHT MATCHING problem on the graph whose vertices are cells and whose
//     edges are candidate swaps.
//
//     That graph is not bipartite.  Two cells may only be swapped
//     simultaneously if they are not neighbours on the grid (otherwise the two
//     swaps interact), so edges join exactly the NON-adjacent pairs of cells --
//     the complement of the 16x16 grid graph.  The complement of a bipartite
//     graph is not bipartite: three cells a-b-c with a-b and b-c grid edges and
//     a-c not a grid edge form a triangle of legal swaps.  Blossom (odd cycle)
//     contraction is therefore unavoidable.  A bipartite/assignment solver such
//     as the Hungarian algorithm cannot express this problem at all.
//
// (2) StubMatchingBound -- an admissible lower bound on the number of
//     mismatched edges of a partially filled board.
//     Give every unplaced piece four "stub" vertices, one per side, and give
//     every already-determined board slot a "demand" vertex carrying the colour
//     that slot requires.  A perfect matching pairs stubs with stubs (an
//     internal adjacency between two unplaced cells) or stubs with demands (an
//     adjacency against a cell that is already decided).  Charge 1 for a pair
//     whose colours disagree and 0 otherwise.  Any legal completion of the board
//     induces such a perfect matching whose cost is exactly its number of
//     mismatches, so the minimum-cost perfect matching is a LOWER BOUND on the
//     number of mismatches that any completion can achieve.
//
//     Because pieces may be rotated freely, a stub carries no direction: any
//     stub may face any way round.  There is therefore no N/S versus E/W
//     typing, the compatibility graph contains triangles (three pieces that each
//     have a red edge), and the matching problem is again non-bipartite.  If
//     rotations were fixed in advance the graph would split into two colour
//     classes and a bipartite solver would do; it is precisely the freedom to
//     rotate that forces us onto blossoms.

#pragma once

#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"

namespace e2 {

// ---------------------------------------------------------------------------
// (1) Optimal packet of disjoint swaps
// ---------------------------------------------------------------------------
struct BlossomSwapParams {
    int maxCells = 96;               // cap on the candidate cell set
    int minCells = 4;                // do nothing if fewer candidates than this
    int maxPairs = 20000;            // cap on candidate swap edges handed to Blossom
    bool filterAdjacent = true;      // drop swaps that would interact with a better one
    bool acceptOnlyImproving = true; // revert the whole packet if it does not help
    bool includePool = false;        // unused: the board is always full
};

struct BlossomSwapResult {
    bool ran = false;
    int cells = 0;         // candidate cells
    int pairsOffered = 0;  // positive-gain swap edges
    int pairsChosen = 0;   // pairs in the optimal matching
    int pairsApplied = 0;  // pairs surviving the adjacency filter
    int predictedGain = 0; // sum of gains the matching believed it would get
    int actualGain = 0;    // measured change of the objective
    int costBefore = 0, costAfter = 0;
    double seconds = 0.0;
    std::string error;
};

BlossomSwapResult BlossomSwapPass(Board& b, const BlossomSwapParams& pr,
                                  MatchingStats* ms = nullptr);

// ---------------------------------------------------------------------------
// (1b) Optimal reassignment of a set of cells
// ---------------------------------------------------------------------------
// The swap packet above is exact but limited to transpositions (2-cycles).  The
// complementary operator is: take a set S of cells, take the pieces currently
// sitting on them as a pool, and ask for the *best permutation* of the pool over
// S -- any permutation, not just a product of disjoint swaps.  Every cell's cost
// is evaluated against the board as it stands, so this is a minimum-weight
// perfect matching between cells and pieces:
//
//        cost(cell c, piece p) = min over rotations of (violated slots of c)
//
// and a perfect matching of the 2|S|-vertex bipartite graph is exactly an
// assignment of one piece to each cell.  Unlike the swap packet the individual
// terms are NOT exact -- all the cells move at once, so each cell's neighbours
// change underneath it -- so the pass is a proposer and the true objective
// change is measured afterwards.
//
// (This sub-problem is bipartite, so a Hungarian solver would be the textbook
// choice; it is solved with the same Blossom VI engine here so that the project
// has exactly one matching dependency.  Blossom VI handles bipartite graphs as
// the special case with no odd cycles.)

struct AssignParams {
    int cells = 64;             // how many cells to re-optimise at once
    bool acceptOnlyImproving = true;
    bool useTopCells = true;    // pick the most violated cells (else random)
};

struct AssignResult {
    bool ran = false;
    int cells = 0;
    int predicted = 0;      // surrogate cost the matching believed it would get
    int actualGain = 0;     // measured change of the objective
    int costBefore = 0, costAfter = 0;
    long long changed = 0;  // cells whose piece actually moved
    double seconds = 0.0;
};

AssignResult AssignReoptimizePass(Board& b, const AssignParams& pr, MatchingStats* ms = nullptr);

// ---------------------------------------------------------------------------
// (2) Stub-matching admissible bound
// ---------------------------------------------------------------------------
struct StubBoundResult {
    int bound = 0;             // admissible lower bound on mismatches (INT_MAX if infeasible)
    bool infeasible = false;   // no completion can exist at all
    int vertices = 0, edges = 0;
    double seconds = 0.0;
};

// `cells`        : cells that still have to be filled (size k)
// `pool`         : pieces available to fill them (size k)
// `req`          : size 4*cells.size(); req[4*i+s] is either
//                    -1                     -> the slot faces another free cell
//                                              (so it will be paired with a stub)
//                    a colour >= 0          -> the slot requires that colour
//                                              (its neighbour is already placed,
//                                              or it is a border slot, which
//                                              requires kGray)
//                  The caller must keep this consistent: if slot (c,s) faces
//                  free cell c', then slot (c', opposite(s)) is also free.
// Returns a lower bound on the number of violated slots (internal mismatches
// plus non-grey border slots) of ANY assignment of `pool` to `cells`.
StubBoundResult StubMatchingBound(const Puzzle& pz, const std::vector<int>& cells,
                                  const std::vector<int>& pool, const std::vector<int>& req,
                                  MatchingStats* ms = nullptr);

// Convenience: build `req` for a set of free cells, given a board whose other
// cells are already placed.  `freeMark`[cell] != 0 means "cell is free".
void BuildRequirements(const Puzzle& pz, const Board& b, const std::vector<int>& cells,
                       const std::vector<char>& freeMark, std::vector<int>& req);

// ---------------------------------------------------------------------------
// (3) Exact re-optimisation of a region, pruned by the stub bound
// ---------------------------------------------------------------------------
struct RegionParams {
    long long maxNodes = 200000;
    bool useStubBound = true;
    int boundMaxDepth = 3;   // apply the expensive bound only in the first levels
    bool useBoundAlwaysBelow = 8;   // but always apply it when <= this many cells remain
    bool requireImprovement = true;
};

struct RegionResult {
    bool ran = false;
    bool improved = false;
    int costBefore = 0, costAfter = 0;
    long long nodes = 0;
    long long boundCalls = 0;
    long long boundPrunes = 0;
    double seconds = 0.0;
};

// Exhaustively re-place the pieces currently sitting on `cells` back onto
// `cells`, minimising the number of violated slots inside the region and on its
// boundary.  Returns true (and leaves the board changed) only if it strictly
// improves.  `maxNodes` bounds the work.
RegionResult RegionReoptimize(Board& b, const std::vector<int>& cells,
                              const RegionParams& pr, MatchingStats* ms = nullptr);

}  // namespace e2
