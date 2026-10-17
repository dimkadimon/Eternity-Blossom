// Eternity-Blossom -- dissection-based exact reassignment.
//
// SPDX-License-Identifier: MIT
//
// WHY INDEPENDENCE MAKES THIS EXACT
// ---------------------------------
// Dissect the board into rigid groups of cells (dominoes, L and Z shapes,
// squares, whole rows or columns).  Pick a set S of groups **no two of which
// share an edge**, freeze every other group, and ask for the best way to
// redistribute the selected groups' contents among the selected groups'
// locations, each with its best rotation.
//
// The independence condition is not a detail, it is the whole point.  Every
// edge whose violation can change has at least one endpoint in S.  If two
// groups of S touched, that shared edge would depend on BOTH of their new
// contents, and the total cost would no longer split into a sum of independent
// per-group terms -- an assignment problem could not express it.  With
// independence, every changing edge has exactly one endpoint in S, so
//
//     total change = sum over g in S of  (boundary cost of slot(g) holding g's
//                                         content in its chosen rotation)
//                  - sum over g in S of  (current boundary cost of slot(g))
//
// and that IS an assignment problem: rows = groups, columns = slots, entry =
// the boundary cost, and the minimum-cost perfect matching gives the optimal
// redistribution.  The second term is a constant, so the Hungarian optimum is
// the true optimum over this whole neighbourhood -- not a proposal to be
// verified afterwards, but a proven best move.
//
// (Contrast with the older AssignReoptimizePass, which reassigns individual
// CELLS.  Cells are adjacent to each other, so their costs are coupled and the
// matching only optimises a surrogate; that operator has to measure the real
// change and roll back if it guessed wrong.  This one does not.)
//
// Two things also make it cheap:
//
//   * A group's INTERNAL edges are untouched.  Rotating a rigid shape carries
//     every internal adjacency onto another internal adjacency with the same
//     pair of colours (verified by the self-test for every shape here), so the
//     move is free with respect to everything the group already gets right.
//   * It is bipartite, so it uses the O(n^3) Hungarian in assign.hpp rather
//     than Blossom VI.  That is roughly 100x faster than routing a bipartite
//     instance through a general matching solver, which is what makes it
//     affordable to run against a thousand different dissections.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "e2/board.hpp"

namespace e2 {

// Rotate a set of cell offsets by q quarter turns clockwise and translate the
// result back to the origin; `dst[i]` is the index (in the canonical row-major
// order of the rotated shape) that source i moves to.  Always succeeds -- see
// the note in dissect.cpp.  Shared with coarse.cpp so that both modules agree
// on what a rigid motion of a shape is.
void RotateShapeOffsets(const std::vector<std::pair<int, int>>& in, int q,
                        std::vector<std::pair<int, int>>& out, std::vector<int>& dst);

// A rigid group of cells.  Two groups may exchange contents only if some
// rotation turns the first into the second's shape (tracked by shapeId).
struct Group {
    int shapeId = 0;
    std::vector<int> cells;              // cells, in canonical (row-major) order
    std::vector<char> rotValid;          // [q] is this quarter-turn grid aligned?
    std::vector<std::vector<int>> dst;   // [q][i] -> index that source i moves to
    std::vector<std::vector<int>> inv;   // [q][j] -> index whose content lands on j
    std::vector<int> inc;                // [q] -> added to each piece's rotation
    std::vector<int> rotShape;           // [q] -> shapeId after rotating by q
};

struct Dissection {
    std::string name;
    std::vector<Group> groups;
    std::vector<int> groupOf;                  // cell -> group, -1 if uncovered
    std::vector<std::vector<int>> adj;         // group -> groups sharing an edge
    std::vector<std::vector<int>> bslots;      // group -> boundary slots, packed index*4+side

    bool adjacent(int a, int b) const {
        const std::vector<int>& v = adj[a];
        return std::binary_search(v.begin(), v.end(), b);
    }
};

// ------------------------------------------------------------------ dissections
// Random 1x2 domino dissection (flip Markov chain from the all-horizontal one).
Dissection MakeDominoDissection(const Puzzle& pz, uint64_t seed, int flips = 20000);

// Aligned s x s blocks.
Dissection MakeSquareDissection(const Puzzle& pz, int side, int phaseR = 0, int phaseC = 0);

// Whole rows / whole columns (1x16 strips -- identity and reversal only).
Dissection MakeRowStripDissection(const Puzzle& pz);
Dissection MakeColumnStripDissection(const Puzzle& pz);

// A greedy mixed dissection using L-trominoes, Z/S-tetrominoes and dominoes,
// with monominoes filling whatever is left over.  `pL`, `pZ`, `pD` are the
// probabilities of trying each shape at a fresh cell.
Dissection MakeMixedDissection(const Puzzle& pz, uint64_t seed,
                               double pL = 0.5, double pZ = 0.3, double pD = 0.2);

// ------------------------------------------------------------------- the move
struct ReassignParams {
    int minGroups = 2;        // do nothing with a smaller independent set than this
    int maxGroups = 64;       // cap the assignment problem
    int maxRotVariants = 4;   // rotations considered per (group, slot) pair
    bool acceptOnlyImproving = true;
    bool shuffle = true;      // randomise the greedy independent set
};

struct ReassignResult {
    bool ran = false;
    bool improved = false;
    int groups = 0;             // size of the independent set
    long long predicted = 0;    // reduction of the objective the Hungarian proved
    long long actualGain = 0;   // measured change of Board::cost
    long long costBefore = 0, costAfter = 0;
    long long assignments = 0;  // Hungarian calls
    double seconds = 0.0;
};

// One exact reassignment on one dissection, on one independently chosen
// independent set.
ReassignResult DissectionReassign(Board& b, const Dissection& d, const ReassignParams& pr,
                                  uint64_t seed);

// Repeatedly reassign on `d` until no further improvement (or `maxRounds`).
struct ReassignRepeatResult {
    int rounds = 0, improvedRounds = 0;
    long long totalGain = 0;
    long long costBefore = 0, costAfter = 0;
    double seconds = 0.0;
};
ReassignRepeatResult DissectionReassignRepeat(Board& b, const Dissection& d,
                                              const ReassignParams& pr, uint64_t seed,
                                              int maxRounds = 200);

}  // namespace e2
