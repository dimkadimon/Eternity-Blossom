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
#include <array>
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
    // group -> its own internal edges, as {idxA, side, idxB}: cell cells[idxA]
    // shares an edge, on side `side`, with cell cells[idxB].  Under a RIGID
    // rotation these are merely permuted, so they cancel and can be ignored --
    // but as soon as pieces are allowed to permute and spin independently the
    // internal edges change too and must be costed explicitly.
    std::vector<std::vector<std::array<int, 3>>> internal;

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

// All distinct orientations of a polyomino, normalised and deduplicated.
// `reflect` also includes the mirror image, which is a DIFFERENT free
// polyomino for S/Z and L/J, so leaving it out loses half the tromino and
// tetromino shapes.
std::vector<std::vector<std::pair<int, int>>> AllOrientations(
    const std::vector<std::pair<int, int>>& base, bool reflect);

// Greedy tiling by n-cell polyominoes, from a list of shapes (give each shape
// in one orientation; AllOrientations will have expanded it already).
// Whatever cannot be covered falls back to monominoes, so the result is always
// a partition.  256 = 3*85 + 1, so trominoes leave exactly one cell over.
Dissection MakePolyominoDissection(const Puzzle& pz, uint64_t seed,
                                   const std::vector<std::vector<std::pair<int, int>>>& shapes);

// Convenience wrappers: all orientations of the 2 trominoes (L and I), and of
// the 5 free tetrominoes (I, O, T, S/Z, L/J).
Dissection MakeTrominoDissection(const Puzzle& pz, uint64_t seed);
Dissection MakeTetrominoDissection(const Puzzle& pz, uint64_t seed);

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

    // Minimise over EVERY way of filling a slot: assign the group's n pieces to
    // the slot's n cells in any of n! orders and spin each of them freely, so
    // 4^n * n! placements per (group, slot) pair instead of the 4 rigid ones.
    //
    // Two consequences worth stating:
    //  * the group's own internal edges now change, so they are costed too;
    //  * a group no longer has to match the slot's SHAPE, only its SIZE --
    //    pieces are loose tiles, so any n-cell group fills any n-cell slot.
    //    The assignment is therefore over all groups of a size, not a shape.
    bool fullRearrange = false;
    int maxCells = 3;         // largest n still brute-forced under fullRearrange
                              // (4^4*4! = 6144 at n=4, 4^5*5! = 122880 at n=5)
    int misTrials = 1;        // randomised greedy restarts for the independent set
    bool misImprove = false;  // local search on the independent set (1-for-2 swaps)
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

// Cost of the CURRENT contents of group g sitting in its own cells, counting
// both its boundary half-edges and its internal edges.  Under fullRearrange the
// baseline the Hungarian improves on has to include the internal edges, because
// the rearrangement is free to change them.
long long CurrentGroupCost(const Board& b, const Dissection& d, int g);

// Cost of filling slot `sj` with the contents of group `gi`, minimised over all
// 4^n * n! placements (n = cells in the slot).  Returns the cost and, if asked,
// the achieving arrangement: perm[k] is the index into gi.cells of the piece
// that lands on slot cell k, rot[k] is the rotation it is given.
long long SlotCostFull(const Board& b, const Dissection& d, int gi, int sj,
                       std::vector<int>* perm = nullptr, std::vector<int>* rot = nullptr);

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
