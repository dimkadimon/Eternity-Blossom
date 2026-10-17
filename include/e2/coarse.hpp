// Eternity-Blossom -- coarse-grained ("chunk") moves for Eternity II.
//
// SPDX-License-Identifier: MIT
//
// THE IDEA
// --------
// Every move the fine-grained search makes -- rotate one piece, swap two cells
// -- is local on the 16x16 lattice, and a local search over such moves stalls
// (measured: 700M iterations without leaving 425/480).  The classical escape is
// to move CHUNKS: partition the board into rigid super-tiles and treat each
// super-tile as a single piece on a coarser board.
//
// Two families are implemented here.
//
// (a) SQUARE BLOCKS.  Dissect the board into 2x2 blocks and you get an 8x8 board
//     of 64 super-tiles; into 4x4 blocks and you get 4x4; into 8x8 and you get
//     2x2.  Two moves become available, both of which are LARGE in the original
//     space and CHEAP to evaluate:
//
//       BLOCK ROTATE -- rotate a block by 90/180/270 degrees about its centre.
//         The block's internal structure is a symmetry of the block, so every
//         internal adjacency is carried onto another internal adjacency
//         (top edge -> right -> bottom -> left for a quarter turn) bringing the
//         same pair of colours with it.  Individual internal edges therefore
//         MOVE, but how many of them match does not change -- a 2x2 block keeps
//         all 4 of its internal edges' statuses, a 4x4 keeps 24, an 8x8 keeps
//         112.  Only the 4s boundary unit-edges can change the objective, so the
//         move is "free" with respect to everything the block already gets right
//         and costs 4s slot lookups to evaluate exactly.  SelfTestCoarse checks
//         both halves of this: the delta, and the invariant count.
//
//       BLOCK SWAP -- translate two blocks onto each other.  All internal edges
//         of BOTH blocks survive (each block's pieces keep their relative
//         arrangement and do not rotate), so 2*(2s^2-2s) edges are untouched and
//         only the 8s boundary edges of the pair change.
//
// (b) DOMINOES (1x2).  There is no single canonical domino dissection of a 16x16
//     board -- there are astronomically many -- and each one induces a DIFFERENT
//     neighbourhood: the flip of a domino exchanges its two cells and turns both
//     pieces by 180 degrees, which preserves the domino's internal edge and
//     re-exposes its six boundary edges.  Sampling a fresh dissection therefore
//     gives the search a fresh set of 128 large moves to try on the same board
//     state, which is a strong diversification device: it escapes local optima
//     that are local only with respect to one particular tiling.
//
// WHY THIS IS A MATCHING PROBLEM (and a non-bipartite one)
// --------------------------------------------------------
// A packet of block swaps may only be applied simultaneously if no two of the
// blocks involved touch, otherwise their gains are not additive.  So, exactly as
// at the cell level, the legal pairs are the NON-adjacent pairs of super-tiles:
// the complement of the coarse adjacency graph.  On the 8x8 block board that
// complement contains triangles (three blocks, no two of which share a unit
// edge), so choosing the best packet is a maximum-weight matching problem on a
// NON-BIPARTITE graph -- the same Blossom VI call, one level up.  For a domino
// tiling (128 vertices) the graph is bigger and even further from bipartite.
//
// This is the multi-level generalisation of BlossomSwapPass: the identical
// primitive, applied to the quotient board.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "e2/board.hpp"
#include "e2/matching.hpp"

namespace e2 {

enum BlockKind {
    kDominoH = 0,   // 1x2, cells (r,c) and (r,c+1)
    kDominoV = 1,   // 2x1, cells (r,c) and (r+1,c)
    kSquare = 2     // s x s, s given by `side`
};

// A rigid super-tile.
struct Block {
    int kind = kSquare;
    int side = 2;            // square side (2/4/8); 2 for dominoes
    int shapeId = 0;         // two blocks may only be swapped if this matches
    int r0 = 0, c0 = 0;      // top-left cell
    std::vector<int> cells;  // cells in row-major order

    int nrot = 4;                          // 4 for squares, 2 for dominoes
    std::vector<std::vector<int>> dst;     // [q][i] -> index that the content of i moves to
    std::vector<std::vector<int>> inv;     // [q][j] -> index whose content lands on j
    std::vector<int> inc;                  // [q]   -> added to every piece's rotation
    // Whether rotation q leaves the shape on the SAME set of cells.  True for
    // all four turns of a square; for a 1x2 domino only 0 and 2 qualify, because
    // a quarter turn carries a horizontal domino onto a vertical pair of cells
    // that belongs to a different domino.  In-place operators (block rotate)
    // must skip the others; operators that move a group to a new location
    // (dissect.hpp) are free to use all four.
    std::vector<char> rotInPlace;
};

// A whole-board dissection into super-tiles.
struct BlockSet {
    std::vector<Block> blocks;
    std::vector<int> blockOf;              // cell -> block index, -1 if uncovered
    std::vector<std::vector<int>> adj;     // block -> blocks sharing a unit edge (sorted)
    std::vector<std::vector<int>> bslots;  // block -> boundary slots, packed as index*4+side

    bool adjacent(int a, int b) const {
        const std::vector<int>& v = adj[a];
        return std::binary_search(v.begin(), v.end(), b);
    }
};

// Dissect into aligned s x s blocks starting at (phaseR, phaseC).  Cells that
// fall outside are simply not covered by any block.
BlockSet MakeSquareBlocks(const Puzzle& pz, int side, int phaseR = 0, int phaseC = 0);

// A random domino dissection.  Starts from the all-horizontal tiling and applies
// `flips` random 2x2 rotations; that Markov chain is connected over the set of
// domino tilings, so a long enough run samples the space properly.
BlockSet MakeDominoTiling(const Puzzle& pz, uint64_t seed, int flips = 20000);

// ---------------------------------------------------------------------------
// Exact deltas.  All of them are in the same units as Board::cost, i.e.
// 2 per mismatched internal edge and wBorderSlot per bad border slot.
// ---------------------------------------------------------------------------

// Change of the objective if block `bi` were given contents newPiece/newRot
// (indexed by position within the block).
int BlockDelta(const Board& b, const BlockSet& bs, int bi,
               const int* newPiece, const int* newRot);

// Rotate block `bi` by q quarter turns (q in [0, nrot)).
int BlockRotateDelta(const Board& b, const BlockSet& bs, int bi, int q);

// Swap blocks `ai` and `bi`, rotating the incoming contents by qa / qb quarter
// turns.  Only meaningful when the two blocks are NOT adjacent; returns 0 (with
// the move treated as unavailable) if they are.
int BlockSwapDelta(const Board& b, const BlockSet& bs, int ai, int bi, int qa, int qb);

void ApplyBlockRotate(Board& b, const BlockSet& bs, int bi, int q);
void ApplyBlockSwap(Board& b, const BlockSet& bs, int ai, int bi, int qa, int qb);

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------
struct CoarseRotateResult {
    int tried = 0;      // (block, rotation) pairs evaluated
    int applied = 0;    // improving ones taken
    long long gain = 0; // total change of the objective (negative is good)
    double seconds = 0.0;
};

// Sweep every block and every non-trivial rotation, taking each improving move
// immediately.  Blocks are visited in a fixed order; deltas are recomputed from
// the live board so the sweep stays correct as it goes.
CoarseRotateResult CoarseRotateSweep(Board& b, const BlockSet& bs, bool acceptEqual = false);

struct CoarseSwapParams {
    int maxBlocks = 64;      // cap on the candidate super-tile set
    int maxPairs = 4000;     // cap on edges handed to Blossom
    int rotVariants = 1;     // 1 = pure swap, 4 = equal rotations (q,q), 16 = all (qa,qb)
    bool acceptOnlyImproving = true;
};

struct CoarseSwapResult {
    bool ran = false;
    int blocks = 0;
    int pairsOffered = 0;   // positive-gain swap edges
    int pairsChosen = 0;    // pairs in the optimal matching
    int pairsApplied = 0;   // pairs surviving the adjacency filter
    long long predictedGain = 0;
    long long actualGain = 0;
    long long costBefore = 0, costAfter = 0;
    double seconds = 0.0;
    std::string error;
};

// Optimal packet of non-interacting block swaps, by maximum-weight matching on
// the complement of the coarse adjacency graph (non-bipartite -- see above).
CoarseSwapResult CoarseSwapPass(Board& b, const BlockSet& bs, const CoarseSwapParams& pr,
                                MatchingStats* ms = nullptr);

}  // namespace e2
