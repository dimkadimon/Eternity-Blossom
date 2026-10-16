// Eternity-Blossom -- incremental board representation for Eternity II.
//
// SPDX-License-Identifier: MIT
//
// The board is always *full*: every cell holds exactly one piece in one of its
// four rotations.  Every search move is therefore a permutation of the pieces
// (a swap of two cells, a rotation in place, or a permutation of a larger set
// produced by an LNS sub-solver), which keeps the objective a single number that
// can be maintained incrementally.
//
// SLOT ACCOUNTING (the subtle part -- read this before touching the deltas)
// -----------------------------------------------------------------------
// There are 4*W*H "slots" (cell, side).  Each slot has
//     a *colour*   -- the colour facing out of that side,
//     a *target*   -- the colour facing it from across the adjacency, or kGray
//                     if the side is on the outer border,
//     a *weight*   -- 1 for an internal slot, W = 2*wBorderRel for a border slot.
// A slot is violated when colour != target.  The objective is the total weight
// of violated slots:
//
//     cost = sum over slots of weight(slot) * [colour != target]
//          = 2 * (mismatched internal edges) + W * (bad border slots)
//
// The factor 2 is because an internal adjacency is seen from both endpoints:
// both of its slots report the same violation.  A border slot belongs to exactly
// one cell.  This asymmetry is why the two delta routines below look different:
//
//   * changing one cell changes its own four slots AND the one slot of each
//     grid neighbour that faces it -- so the change of `cost` is
//          2 * (change in the cell's violated INTERNAL slots)
//        + W * (change in the cell's violated BORDER slots)
//     which is `cellCostIfF` below (F = "fast", one cell at a time).  This is
//     the routine the hot loop uses, because it needs no neighbour chasing.
//
//   * the general routine (`generalSwapDelta`) instead sums the per-slot change
//     (A = "additive", weight 1 per internal slot) over every affected cell.
//     It is slower but is valid for adjacent cells, whose cost terms are coupled
//     through the shared edge.
//
// Both are in the same units; SelfTestIncrements checks them against each other
// and against a full rebuild.
//
// A perfect solution has cost 0: all 480 internal edges of the 16x16 board match
// and all 64 border slots are grey.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include "e2/puzzle.hpp"

namespace e2 {

class Board {
public:
    // Cost of one non-grey border slot RELATIVE to one mismatched internal edge.
    int wBorderRel = 8;
    int wBorderSlot = 16;   // = 2*wBorderRel, the slot weight used in `cost`

    const Puzzle* pz = nullptr;
    std::vector<int> pc;         // [cell]  -> piece id
    std::vector<int> rt;         // [cell]  -> rotation 0..3
    std::vector<int> loc;        // [piece] -> cell
    std::vector<uint8_t> col;      // [cell*4+side] -> colour facing out
    std::vector<uint8_t> target;   // [cell*4+side] -> required colour (kGray on border)
    std::vector<uint8_t> borderMask;  // [cell] -> bit s set iff side s is a border slot
    std::vector<int> cellCostA;   // [cell] -> ci + wBorderSlot*cb
    std::vector<int> cellCostF;   // [cell] -> 2*ci + wBorderSlot*cb   (single-cell delta unit)
    std::vector<int> cellCostInt; // [cell] -> ci, violated internal slots
    std::vector<int> cellCostBor; // [cell] -> cb, violated border slots
    std::vector<uint8_t> protCol;  // [piece*16 + rot*4 + side] -> colour
    long long cost = 0;          // objective (see above)
    long long nbbad = 0;         // number of bad border slots

    Board() = default;
    explicit Board(const Puzzle& p) { init(p); }

    void setBorderWeight(int rel) {
        wBorderRel = rel;
        wBorderSlot = 2 * rel;
    }

    void init(const Puzzle& p) {
        pz = &p;
        pc.assign(p.ncells, kNoPiece);
        rt.assign(p.ncells, 0);
        loc.assign(p.ncells, kNoPiece);
        col.assign(p.ncells * 4, kGray);
        target.assign(p.ncells * 4, kGray);
        cellCostA.assign(p.ncells, 0);
        cellCostF.assign(p.ncells, 0);
        cellCostInt.assign(p.ncells, 0);
        cellCostBor.assign(p.ncells, 0);
        borderMask.assign(p.ncells, 0);
        protCol.assign(p.ncells * 16, 0);
        for (int pi = 0; pi < p.ncells; ++pi) {
            for (int r = 0; r < 4; ++r) {
                for (int s = 0; s < 4; ++s) {
                    protCol[pi * 16 + r * 4 + s] = (uint8_t)sideColor(p.pieces[pi].c, s, r);
                }
            }
        }
        for (int cell = 0; cell < p.ncells; ++cell) {
            unsigned m = 0;
            for (int s = 0; s < 4; ++s) {
                if (p.neigh[cell * 4 + s] < 0) m |= (1u << s);
            }
            borderMask[cell] = (uint8_t)m;
        }
        cost = 0; nbbad = 0;
    }

    // ---------------------------------------------------------------- state
    long long mismatches() const { return (cost - wBorderSlot * nbbad) / 2; }
    long long score() const { return (long long)pz->numEdges() - mismatches(); }
    long long maxScore() const { return (long long)pz->numEdges(); }

    // ------------------------------------------------------------- (re)build
    void placeRaw(int cell, int piece, int rot) {
        pc[cell] = piece;
        rt[cell] = rot;
        loc[piece] = cell;
    }

    void rebuild() {
        const int n = pz->ncells;
        for (int cell = 0; cell < n; ++cell) {
            const int p = pc[cell];
            const uint8_t* src = &protCol[p * 16 + rt[cell] * 4];
            for (int s = 0; s < 4; ++s) col[cell * 4 + s] = src[s];
            loc[p] = cell;
        }
        for (int cell = 0; cell < n; ++cell) {
            for (int s = 0; s < 4; ++s) {
                const int nb = pz->neigh[cell * 4 + s];
                target[cell * 4 + s] = (nb < 0) ? (uint8_t)kGray : col[nb * 4 + oppositeSide(s)];
            }
        }
        cost = 0; nbbad = 0;
        for (int cell = 0; cell < n; ++cell) {
            const int base = cell * 4;
            const unsigned m = borderMask[cell];
            int ci = 0, cb = 0;
            for (int s = 0; s < 4; ++s) {
                const int v = (col[base + s] != target[base + s]) ? 1 : 0;
                if (m & (1u << s)) cb += v; else ci += v;
            }
            cellCostInt[cell] = ci;
            cellCostBor[cell] = cb;
            cellCostA[cell] = ci + wBorderSlot * cb;
            cellCostF[cell] = 2 * ci + wBorderSlot * cb;
            cost += cellCostA[cell];
            nbbad += cb;
        }
    }

    // --------------------------------------------- per-cell costs (hot path)
    // F-value of placing `piece` in `rot` on `cell`: the change of `cost` that
    // would result, measured from zero.  (See the header comment.)
    inline int cellCostIfF(int cell, int piece, int rot) const {
        const int base = cell * 4;
        const uint8_t* p4 = &protCol[piece * 16 + rot * 4];
        const unsigned m = borderMask[cell];
        int ci = 0, cb = 0;
        if (m == 0) {
            ci = (p4[0] != target[base + 0]) + (p4[1] != target[base + 1]) +
                 (p4[2] != target[base + 2]) + (p4[3] != target[base + 3]);
        } else {
            for (int s = 0; s < 4; ++s) {
                const int v = (p4[s] != target[base + s]) ? 1 : 0;
                if (m & (1u << s)) cb += v; else ci += v;
            }
        }
        return 2 * ci + wBorderSlot * cb;
    }

    // A-value: per-slot additive cost, used by the general delta.
    inline int cellCostIfA(int cell, int piece, int rot) const {
        const int base = cell * 4;
        const uint8_t* p4 = &protCol[piece * 16 + rot * 4];
        const unsigned m = borderMask[cell];
        int ci = 0, cb = 0;
        for (int s = 0; s < 4; ++s) {
            const int v = (p4[s] != target[base + s]) ? 1 : 0;
            if (m & (1u << s)) cb += v; else ci += v;
        }
        return ci + wBorderSlot * cb;
    }

    // ------------------------------------------------------------- deltas
    // Fast path (cells not grid-adjacent): the two cells' terms are independent,
    // so the best rotation for each can be chosen separately -- 4+4 evaluations
    // instead of 4x4 -- and the deltas simply add.
    inline int fastSwapDelta(int a, int b, int* ra_out, int* rb_out) const {
        const int pa = pc[a], pb = pc[b];
        const int perA = pz->pieces[pa].period, perB = pz->pieces[pb].period;
        int bestA = std::numeric_limits<int>::max(), brb = 0;
        for (int r = 0; r < perB; ++r) {
            const int c = cellCostIfF(a, pb, r);
            if (c < bestA) { bestA = c; brb = r; }
        }
        int bestB = std::numeric_limits<int>::max(), bra = 0;
        for (int r = 0; r < perA; ++r) {
            const int c = cellCostIfF(b, pa, r);
            if (c < bestB) { bestB = c; bra = r; }
        }
        *ra_out = bra; *rb_out = brb;
        return (bestA - cellCostF[a]) + (bestB - cellCostF[b]);
    }

    int generalSwapDelta(int a, int b, int ra, int rb) const {
        const int pa = pc[a], pb = pc[b];
        int nca[4], ncb[4];
        for (int s = 0; s < 4; ++s) {
            nca[s] = protCol[pb * 16 + rb * 4 + s];
            ncb[s] = protCol[pa * 16 + ra * 4 + s];
        }
        int cells[10];
        const int k = affectedCells(a, b, cells);
        int delta = 0;
        for (int i = 0; i < k; ++i) {
            delta += cellCostSimA(cells[i], a, nca, b, ncb) - cellCostA[cells[i]];
        }
        return delta;
    }

    int deltaSwapExact(int a, int b, int ra, int rb) const { return generalSwapDelta(a, b, ra, rb); }

    int bestSwapRotation(int a, int b, int* ra_out, int* rb_out) const {
        if (!adjacent(a, b)) return fastSwapDelta(a, b, ra_out, rb_out);
        const int pa = pc[a], pb = pc[b];
        const int perA = pz->pieces[pa].period, perB = pz->pieces[pb].period;
        int best = std::numeric_limits<int>::max(), bra = 0, brb = 0;
        for (int ra = 0; ra < perA; ++ra) {
            for (int rb = 0; rb < perB; ++rb) {
                const int d = generalSwapDelta(a, b, ra, rb);
                if (d < best) { best = d; bra = ra; brb = rb; }
            }
        }
        *ra_out = bra; *rb_out = brb;
        return best;
    }

    int deltaRotate(int cell, int rot) const {
        return cellCostIfF(cell, pc[cell], rot) - cellCostF[cell];
    }

    // ------------------------------------------------------- 3-cycle moves
    // A swap is a 2-cycle.  A 3-cycle rotates three pieces round three cells
    // (piece at b moves to a, piece at c moves to b, piece at a moves to c).
    // It reaches configurations no sequence of improving swaps can: all three
    // individual 2-cycles can be uphill while the 3-cycle is downhill.
    // When the three cells are pairwise non-adjacent their cost terms are
    // independent, so each rotation is chosen separately -- 4+4+4 evaluations,
    // barely more than the 4+4 of a swap.
    inline bool pairwiseNonAdjacent3(int a, int b, int c) const {
        return !adjacent(a, b) && !adjacent(b, c) && !adjacent(a, c);
    }

    inline int cycle3Delta(int a, int b, int c, int* rA, int* rB, int* rC) const {
        const int pa = pc[a], pb = pc[b], pcx = pc[c];
        int best = std::numeric_limits<int>::max();
        const int perB = pz->pieces[pb].period;
        for (int r = 0; r < perB; ++r) {
            const int v = cellCostIfF(a, pb, r);
            if (v < best) { best = v; *rA = r; }
        }
        int d = best - cellCostF[a];
        best = std::numeric_limits<int>::max();
        const int perC = pz->pieces[pcx].period;
        for (int r = 0; r < perC; ++r) {
            const int v = cellCostIfF(b, pcx, r);
            if (v < best) { best = v; *rB = r; }
        }
        d += best - cellCostF[b];
        best = std::numeric_limits<int>::max();
        const int perA = pz->pieces[pa].period;
        for (int r = 0; r < perA; ++r) {
            const int v = cellCostIfF(c, pa, r);
            if (v < best) { best = v; *rC = r; }
        }
        d += best - cellCostF[c];
        return d;
    }

    void applyCycle3(int a, int b, int c, int rA, int rB, int rC) {
        const int pa = pc[a], pb = pc[b], pcx = pc[c];
        for (int s = 0; s < 4; ++s) col[a * 4 + s] = protCol[pb * 16 + rA * 4 + s];
        for (int s = 0; s < 4; ++s) col[b * 4 + s] = protCol[pcx * 16 + rB * 4 + s];
        for (int s = 0; s < 4; ++s) col[c * 4 + s] = protCol[pa * 16 + rC * 4 + s];
        propagate(a); propagate(b); propagate(c);
        int cells[16];
        int k = 0;
        const int src[3] = {a, b, c};
        for (int t = 0; t < 3; ++t) {
            const int x = src[t];
            bool dup = false;
            for (int i = 0; i < k; ++i) if (cells[i] == x) dup = true;
            if (!dup) cells[k++] = x;
            for (int s = 0; s < 4; ++s) {
                const int nb = pz->neigh[x * 4 + s];
                if (nb < 0) continue;
                dup = false;
                for (int i = 0; i < k; ++i) if (cells[i] == nb) dup = true;
                if (!dup) cells[k++] = nb;
            }
        }
        for (int i = 0; i < k; ++i) refreshCell(cells[i]);
        pc[a] = pb;  rt[a] = rA;  loc[pb] = a;
        pc[b] = pcx; rt[b] = rB;  loc[pcx] = b;
        pc[c] = pa;  rt[c] = rC;  loc[pa] = c;
    }

    int bestRotation(int cell) const {
        const int p = pc[cell];
        const int per = pz->pieces[p].period;
        int best = std::numeric_limits<int>::max(), br = 0;
        for (int r = 0; r < per; ++r) {
            const int c = cellCostIfF(cell, p, r);
            if (c < best) { best = c; br = r; }
        }
        return br;
    }

    // ------------------------------------------------------------- apply
    void applySwap(int a, int b, int ra, int rb) {
        const int pa = pc[a], pb = pc[b];
        int nca[4], ncb[4];
        for (int s = 0; s < 4; ++s) {
            nca[s] = protCol[pb * 16 + rb * 4 + s];
            ncb[s] = protCol[pa * 16 + ra * 4 + s];
        }
        for (int s = 0; s < 4; ++s) col[a * 4 + s] = (uint8_t)nca[s];
        for (int s = 0; s < 4; ++s) col[b * 4 + s] = (uint8_t)ncb[s];
        propagate(a);
        propagate(b);
        int cells[10];
        const int k = affectedCells(a, b, cells);
        for (int i = 0; i < k; ++i) refreshCell(cells[i]);
        pc[a] = pb; rt[a] = rb; loc[pb] = a;
        pc[b] = pa; rt[b] = ra; loc[pa] = b;
    }

    void applyRotation(int cell, int rot) {
        const int p = pc[cell];
        for (int s = 0; s < 4; ++s) col[cell * 4 + s] = protCol[p * 16 + rot * 4 + s];
        propagate(cell);
        int cells[10];
        const int k = affectedCells(cell, cell, cells);
        for (int i = 0; i < k; ++i) refreshCell(cells[i]);
        rt[cell] = rot;
    }

    // ------------------------------------------------------------- helpers
    inline bool adjacent(int a, int b) const {
        for (int s = 0; s < 4; ++s) if (pz->neigh[a * 4 + s] == b) return true;
        return false;
    }

    // Push this cell's colours into its neighbours' targets.
    inline void propagate(int cell) {
        for (int s = 0; s < 4; ++s) {
            const int nb = pz->neigh[cell * 4 + s];
            if (nb >= 0) target[nb * 4 + oppositeSide(s)] = col[cell * 4 + s];
        }
    }

    inline int affectedCells(int a, int b, int* out) const {
        int k = 0;
        out[k++] = a;
        if (b != a) out[k++] = b;
        const int first[2] = {a, b};
        for (int t = 0; t < 2; ++t) {
            const int c = first[t];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz->neigh[c * 4 + s];
                if (nb < 0) continue;
                bool dup = false;
                for (int i = 0; i < k; ++i) if (out[i] == nb) dup = true;
                if (!dup) out[k++] = nb;
            }
            if (b == a) break;
        }
        return k;
    }

    // A-value of `cell` under the simulated colours nca/ncb for cells a/b.
    inline int cellCostSimA(int cell, int a, const int nca[4], int b, const int ncb[4]) const {
        const int base = cell * 4;
        const unsigned m = borderMask[cell];
        int ci = 0, cb = 0;
        for (int s = 0; s < 4; ++s) {
            int tv = target[base + s];
            const int nb = pz->neigh[base + s];
            if (nb == a) tv = nca[oppositeSide(s)];
            else if (nb == b) tv = ncb[oppositeSide(s)];
            const int cv = (cell == a) ? nca[s] : (cell == b) ? ncb[s] : col[base + s];
            const int v = (cv != tv) ? 1 : 0;
            if (m & (1u << s)) cb += v; else ci += v;
        }
        return ci + wBorderSlot * cb;
    }

    inline void refreshCell(int cell) {
        const int base = cell * 4;
        const unsigned m = borderMask[cell];
        int ci = 0, cb = 0;
        for (int s = 0; s < 4; ++s) {
            const int v = (col[base + s] != target[base + s]) ? 1 : 0;
            if (m & (1u << s)) cb += v; else ci += v;
        }
        cost += (ci + wBorderSlot * cb) - cellCostA[cell];
        nbbad += cb - cellCostBor[cell];
        cellCostInt[cell] = ci;
        cellCostBor[cell] = cb;
        cellCostA[cell] = ci + wBorderSlot * cb;
        cellCostF[cell] = 2 * ci + wBorderSlot * cb;
    }

    // ------------------------------------------------------- construction
    template <class RNG>
    void randomize(RNG& rng) {
        const int n = pz->ncells;
        std::vector<int> perm(n);
        std::iota(perm.begin(), perm.end(), 0);
        std::shuffle(perm.begin(), perm.end(), rng);
        for (int cell = 0; cell < n; ++cell) placeRaw(cell, perm[cell], (int)(rng() % 4));
        rebuild();
    }

    // Structured random start: the 4 two-grey pieces go to the corners and the
    // 56 one-grey pieces to the perimeter, each with its grey edge(s) facing
    // out; the remaining 196 pieces fill the interior at random.  This starts
    // with a legal border, so the search can spend its whole budget on the 480
    // internal edges -- the only thing the record score measures.
    template <class RNG>
    void randomizeStructured(RNG& rng) {
        const Puzzle& p = *pz;
        const int n = p.ncells;
        std::vector<int> corners = p.cornerPieces;
        std::vector<int> edges = p.edgePieces;
        std::shuffle(corners.begin(), corners.end(), rng);
        std::shuffle(edges.begin(), edges.end(), rng);

        std::vector<int> cornerCells = {0, p.W - 1, (p.H - 1) * p.W, p.H * p.W - 1};
        std::vector<int> edgeCells;
        for (int c = 1; c < p.W - 1; ++c) edgeCells.push_back(c);
        for (int r = 1; r < p.H - 1; ++r) edgeCells.push_back(r * p.W + p.W - 1);
        for (int c = 1; c < p.W - 1; ++c) edgeCells.push_back((p.H - 1) * p.W + c);
        for (int r = 1; r < p.H - 1; ++r) edgeCells.push_back(r * p.W);
        std::shuffle(edgeCells.begin(), edgeCells.end(), rng);

        auto outward = [&](int cell, std::vector<int>& out) {
            out.clear();
            const int r = cell / p.W, c = cell % p.W;
            if (r == 0) out.push_back(0);
            if (c == p.W - 1) out.push_back(1);
            if (r == p.H - 1) out.push_back(2);
            if (c == 0) out.push_back(3);
        };
        auto rotWithGrayOn = [&](int pi, int s) {
            for (int r = 0; r < 4; ++r) if (protCol[pi * 16 + r * 4 + s] == kGray) return r;
            return 0;
        };
        auto rotWithGrayOn2 = [&](int pi, int s1, int s2) {
            for (int r = 0; r < 4; ++r) {
                if (protCol[pi * 16 + r * 4 + s1] == kGray && protCol[pi * 16 + r * 4 + s2] == kGray) return r;
            }
            return 0;
        };
        std::vector<int> outs;
        for (size_t i = 0; i < cornerCells.size() && i < corners.size(); ++i) {
            const int cell = cornerCells[i];
            outward(cell, outs);
            const int pi = corners[i];
            placeRaw(cell, pi, (outs.size() == 2) ? rotWithGrayOn2(pi, outs[0], outs[1])
                                                  : rotWithGrayOn(pi, outs[0]));
        }
        for (size_t i = 0; i < edgeCells.size() && i < edges.size(); ++i) {
            const int cell = edgeCells[i];
            outward(cell, outs);
            const int pi = edges[i];
            placeRaw(cell, pi, rotWithGrayOn(pi, outs[0]));
        }
        std::vector<int> leftovers;
        std::vector<char> used(n, 0);
        for (int cell = 0; cell < n; ++cell) if (pc[cell] >= 0) used[pc[cell]] = 1;
        for (int pi = 0; pi < n; ++pi) if (!used[pi]) leftovers.push_back(pi);
        std::shuffle(leftovers.begin(), leftovers.end(), rng);
        size_t li = 0;
        for (int cell = 0; cell < n; ++cell) {
            if (pc[cell] == kNoPiece) placeRaw(cell, leftovers[li++], (int)(rng() % 4));
        }
        rebuild();
    }

    // Greedy construction: visit cells in `order` and pick, for each, the
    // remaining (piece, rotation) that minimises the violated weight against
    // the already-decided neighbours and the border.  `noise` is the chance of
    // taking a random pick from the top-k instead of the single best, which
    // gives the restarts in a portfolio some diversity.
    template <class RNG>
    void greedyConstruct(RNG& rng, double noise = 0.0, int topk = 4,
                         const std::vector<int>* order = nullptr) {
        const Puzzle& p = *pz;
        const int n = p.ncells;
        std::vector<int> ord;
        if (order) ord = *order;
        else { ord.resize(n); std::iota(ord.begin(), ord.end(), 0); }

        std::vector<int> remaining(n);
        std::iota(remaining.begin(), remaining.end(), 0);
        std::shuffle(remaining.begin(), remaining.end(), rng);

        for (int idx = 0; idx < n; ++idx) {
            const int cell = ord[idx];
            int conSide[4], conCol[4], conW[4];
            int nc = 0;
            for (int s = 0; s < 4; ++s) {
                const int nb = p.neigh[cell * 4 + s];
                if (nb >= 0) {
                    if (pc[nb] != kNoPiece) {
                        conSide[nc] = s; conCol[nc] = col[nb * 4 + oppositeSide(s)]; conW[nc] = 2; ++nc;
                    }
                } else {
                    conSide[nc] = s; conCol[nc] = kGray; conW[nc] = wBorderSlot; ++nc;
                }
            }
            const int m = (int)remaining.size();
            int bestVal = std::numeric_limits<int>::max(), bestPi = 0, bestRot = 0;
            std::vector<std::pair<int, int>> cand;
            std::vector<int> candRot;
            for (int i = 0; i < m; ++i) {
                const int pi = remaining[i];
                const int per = p.pieces[pi].period;
                for (int r = 0; r < per; ++r) {
                    int val = 0;
                    for (int j = 0; j < nc; ++j) {
                        if (protCol[pi * 16 + r * 4 + conSide[j]] != conCol[j]) val += conW[j];
                    }
                    if (val < bestVal) { bestVal = val; bestPi = i; bestRot = r; }
                    if (noise > 0.0) { cand.emplace_back(val, i); candRot.push_back(r); }
                }
            }
            if (noise > 0.0 && !cand.empty() &&
                std::uniform_real_distribution<double>(0, 1)(rng) < noise) {
                std::vector<int> idxs(cand.size());
                std::iota(idxs.begin(), idxs.end(), 0);
                const int lim = std::min(topk, (int)idxs.size());
                std::nth_element(idxs.begin(), idxs.begin() + lim - 1, idxs.end(),
                                 [&](int x, int y) { return cand[x].first < cand[y].first; });
                const int take = idxs[rng() % lim];
                bestPi = cand[take].second;
                bestRot = candRot[take];
            }
            const int piece = remaining[bestPi];
            remaining[bestPi] = remaining.back();
            remaining.pop_back();
            placeRaw(cell, piece, bestRot);
            for (int s = 0; s < 4; ++s) col[cell * 4 + s] = protCol[piece * 16 + bestRot * 4 + s];
        }
        rebuild();
    }

    void save(std::vector<int>& pc_out, std::vector<int>& rt_out) const {
        pc_out = pc; rt_out = rt;
    }
    void load(const std::vector<int>& pc_in, const std::vector<int>& rt_in) {
        pc = pc_in; rt = rt_in; rebuild();
    }
};

}  // namespace e2
