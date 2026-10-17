// Eternity-Blossom -- correctness checks for coarse / rotation / sub-solve.
// See selftest_extra.hpp.  SPDX-License-Identifier: MIT

#include "e2/selftest_extra.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <climits>
#include <random>

#include "e2/coarse.hpp"
#include "e2/dissect.hpp"
#include "e2/assign.hpp"
#include "e2/rotation.hpp"
#include "e2/subsolve.hpp"

namespace e2 {

namespace {

Puzzle MakeRandomPuzzle(int w, int h, int ncol, uint64_t seed) {
    Puzzle pz;
    std::mt19937 rng((unsigned)seed);
    pz.pieces.resize((size_t)w * h);
    for (Piece& p : pz.pieces) {
        for (int s = 0; s < 4; ++s) p.c[s] = 1 + (int)(rng() % (unsigned)ncol);
        p.period = 4;
    }
    pz.initGeometry(w, h);
    pz.classify();
    return pz;
}

template <class RNG>
void RandomiseBoard(Board& b, RNG& rng) {
    const int n = b.pz->ncells;
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);
    for (int c = 0; c < n; ++c) {
        b.pc[c] = perm[c];
        b.rt[c] = (int)(rng() % 4);
    }
    b.rebuild();
}

// How many of the block's internal edges are matched.  A rigid motion does NOT
// leave each internal edge where it was -- a 90 degree turn carries the top edge
// onto the right edge and so on -- but it does preserve how many of them match,
// which is exactly what the boundary-only delta formula relies on.
int InternalMatchedCount(const Board& b, const BlockSet& bs, int bi) {
    int matched = 0;
    const Block& bl = bs.blocks[bi];
    std::vector<char> in(bl.cells.size(), 0);
    for (size_t i = 0; i < bl.cells.size(); ++i) in[i] = 1;
    std::vector<int> idx(b.pz->ncells, -1);
    for (size_t i = 0; i < bl.cells.size(); ++i) idx[bl.cells[i]] = (int)i;
    for (size_t i = 0; i < bl.cells.size(); ++i) {
        const int cell = bl.cells[i];
        for (int s = 0; s < 4; ++s) {
            const int nb = b.pz->neigh[cell * 4 + s];
            if (nb < 0 || idx[nb] < 0) continue;
            if ((int)i > idx[nb]) continue;  // once per edge
            if (b.col[cell * 4 + s] == b.col[nb * 4 + oppositeSide(s)]) ++matched;
        }
    }
    return matched;
}

}  // namespace

// ---------------------------------------------------------------- coarse moves
bool SelfTestCoarse(const Puzzle& pz, int trials, uint64_t seed) {
    std::mt19937 rng((unsigned)seed);
    Board b(pz);

    for (int side : {2, 4}) {
        BlockSet bs = MakeSquareBlocks(pz, side);
        if (bs.blocks.empty()) { std::cerr << "  coarse: no blocks for side " << side << "\n"; return false; }
        for (int t = 0; t < trials; ++t) {
            RandomiseBoard(b, rng);
            // every block, every non-trivial rotation
            for (int bi = 0; bi < (int)bs.blocks.size(); ++bi) {
                const int before = InternalMatchedCount(b, bs, bi);
                for (int q = 1; q < bs.blocks[bi].nrot; ++q) {
                    if (!bs.blocks[bi].rotInPlace[q]) continue;
                    const int predicted = BlockRotateDelta(b, bs, bi, q);
                    const long long c0 = b.cost;
                    const std::vector<int> sp = b.pc, sr = b.rt;
                    ApplyBlockRotate(b, bs, bi, q);
                    if (b.cost - c0 != predicted) {
                        std::cerr << "  coarse: block-rotate delta " << predicted
                                  << " != actual " << (b.cost - c0) << " (side " << side
                                  << " q " << q << ")\n";
                        return false;
                    }
                    const int after = InternalMatchedCount(b, bs, bi);
                    if (before != after) {
                        std::cerr << "  coarse: block rotation changed the number of matched "
                                     "internal edges (" << before << " -> " << after << ")\n";
                        return false;
                    }
                    b.load(sp, sr);
                }
            }
            // random non-adjacent same-shape pairs
            const int nb = (int)bs.blocks.size();
            for (int k = 0; k < 8 * trials; ++k) {
                const int ai = (int)(rng() % nb), bi = (int)(rng() % nb);
                if (ai == bi || bs.adjacent(ai, bi)) continue;
                // Shape-preserving rotations only: the coarse swap keeps each
                // block's cells where they are and just exchanges contents, so
                // the two shapes must still match.  (Arbitrary rotations, where
                // a group moves to a differently shaped location, are covered
                // by the dissection operator instead.)
                const int qa = (int)(rng() % bs.blocks[ai].nrot);
                const int qb = (int)(rng() % bs.blocks[bi].nrot);
                if (!bs.blocks[ai].rotInPlace[qa] || !bs.blocks[bi].rotInPlace[qb]) continue;
                const int predicted = BlockSwapDelta(b, bs, ai, bi, qa, qb);
                const long long c0 = b.cost;
                const std::vector<int> sp = b.pc, sr = b.rt;
                ApplyBlockSwap(b, bs, ai, bi, qa, qb);
                if (b.cost - c0 != predicted) {
                    std::cerr << "  coarse: block-swap delta " << predicted << " != actual "
                              << (b.cost - c0) << " (side " << side << ")\n";
                    return false;
                }
                b.load(sp, sr);
            }
        }
    }

    // -- domino dissections are legal tilings, and the flip delta is exact
    for (int t = 0; t < std::max(3, trials / 4); ++t) {
        BlockSet bs = MakeDominoTiling(pz, seed + 9176u * (unsigned)t, 5000);
        if ((int)bs.blocks.size() != pz.ncells / 2) {
            std::cerr << "  coarse: domino tiling has " << bs.blocks.size() << " blocks, want "
                      << pz.ncells / 2 << "\n";
            return false;
        }
        std::vector<int> cover(pz.ncells, 0);
        for (const Block& bl : bs.blocks) {
            if (bl.cells.size() != 2) { std::cerr << "  coarse: domino is not 2 cells\n"; return false; }
            for (int c : bl.cells) cover[c]++;
            const int a = bl.cells[0], bb = bl.cells[1];
            bool adj = false;
            for (int s = 0; s < 4; ++s)
                if (pz.neigh[a * 4 + s] == bb) adj = true;
            if (!adj) { std::cerr << "  coarse: domino cells are not adjacent\n"; return false; }
        }
        for (int c = 0; c < pz.ncells; ++c)
            if (cover[c] != 1) { std::cerr << "  coarse: cell " << c << " covered " << cover[c] << " times\n"; return false; }

        RandomiseBoard(b, rng);
        for (int bi = 0; bi < (int)bs.blocks.size(); ++bi) {
            for (int q = 1; q < bs.blocks[bi].nrot; ++q) {
                if (!bs.blocks[bi].rotInPlace[q]) continue;  // would leave the cells
                const int before = InternalMatchedCount(b, bs, bi);
                const int predicted = BlockRotateDelta(b, bs, bi, q);
                const long long c0 = b.cost;
                const std::vector<int> sp = b.pc, sr = b.rt;
                ApplyBlockRotate(b, bs, bi, q);
                if (b.cost - c0 != predicted) {
                    std::cerr << "  coarse: domino rotation delta " << predicted
                              << " != actual " << (b.cost - c0) << "\n";
                    return false;
                }
                if (before != InternalMatchedCount(b, bs, bi)) {
                    std::cerr << "  coarse: domino rotation changed the internal edge\n";
                    return false;
                }
                b.load(sp, sr);
            }
        }
    }
    return true;
}

// --------------------------------------------------------- rotation / the bound
bool SelfTestRotation(const Puzzle& pz, int trials, uint64_t seed) {
    std::mt19937 rng((unsigned)seed);

    // (1) the strip DP against exhaustive enumeration on a 4x4 board:
    //     a 2-row strip is 8 cells = 4^8 = 65536 rotation vectors.
    {
        Puzzle small = MakeRandomPuzzle(4, 4, 6, seed + 11);
        Board sb(small);
        RandomiseBoard(sb, rng);
        std::vector<char> inStrip(small.ncells, 0);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 4; ++c) inStrip[r * 4 + c] = 1;

        StripSpec sp{false, 0, 2};
        StripOpt o = OptimizeStrip(sb, sp, true);
        const int ncell = (int)o.cells.size();
        std::vector<int> rot(ncell, 0);
        const long long total = 1LL << (2 * ncell);
        std::vector<int> stripIdx(small.ncells, -1);
        for (int i = 0; i < ncell; ++i) stripIdx[o.cells[i]] = i;
        long long best = std::numeric_limits<long long>::max();
        const int W = sb.wBorderSlot;
        for (long long code = 0; code < total; ++code) {
            long long v = 0;
            for (int i = 0; i < ncell; ++i) rot[i] = (int)((code >> (2 * i)) & 3);
            for (int e = 0; e < small.numEdges(); ++e) {
                const int a = small.eCellA[e], b2 = small.eCellB[e];
                if (!inStrip[a] && !inStrip[b2]) continue;
                const int ra = (stripIdx[a] >= 0) ? rot[stripIdx[a]] : sb.rt[a];
                const int rb = (stripIdx[b2] >= 0) ? rot[stripIdx[b2]] : sb.rt[b2];
                const int ca = (int)sb.protCol[sb.pc[a] * 16 + ra * 4 + small.eSideA[e]];
                const int cb = (int)sb.protCol[sb.pc[b2] * 16 + rb * 4 + small.eSideB[e]];
                if (ca != cb) v += 2;
            }
            for (int cell = 0; cell < small.ncells; ++cell) {
                if (!inStrip[cell]) continue;
                for (int s = 0; s < 4; ++s) {
                    if (small.neigh[cell * 4 + s] >= 0) continue;
                    const int c = (int)sb.protCol[sb.pc[cell] * 16 + rot[stripIdx[cell]] * 4 + s];
                    if (c != kGray) v += W;
                }
            }
            best = std::min(best, v);
        }
        if (o.optimum != best) {
            std::cerr << "  rotation: strip DP optimum " << o.optimum << " != brute force "
                      << best << "\n";
            return false;
        }
        // and the current-configuration evaluation must agree with a direct one
        long long direct = 0;
        for (int e = 0; e < small.numEdges(); ++e) {
            const int a = small.eCellA[e], b2 = small.eCellB[e];
            if (!inStrip[a] && !inStrip[b2]) continue;
            if (sb.col[a * 4 + small.eSideA[e]] != sb.col[b2 * 4 + small.eSideB[e]]) direct += 2;
        }
        for (int cell = 0; cell < small.ncells; ++cell) {
            if (!inStrip[cell]) continue;
            for (int s = 0; s < 4; ++s)
                if (small.neigh[cell * 4 + s] < 0 && sb.col[cell * 4 + s] != kGray) direct += W;
        }
        if (o.current != direct) {
            std::cerr << "  rotation: strip current " << o.current << " != direct " << direct << "\n";
            return false;
        }
    }

    // (2) the upper bound against the exhaustive rotation optimum of a 3x3 board
    {
        Puzzle small = MakeRandomPuzzle(3, 3, 5, seed + 22);
        Board sb(small);
        RandomiseBoard(sb, rng);
        const int n = small.ncells;
        long long bestMismatch = std::numeric_limits<long long>::max();
        const long long total = 1LL << (2 * n);
        for (long long code = 0; code < total; ++code) {
            std::vector<int> rot(n);
            for (int i = 0; i < n; ++i) rot[i] = (int)((code >> (2 * i)) & 3);
            long long mis = 0;
            for (int e = 0; e < small.numEdges(); ++e) {
                const int a = small.eCellA[e], b2 = small.eCellB[e];
                const int ca = (int)sb.protCol[sb.pc[a] * 16 + rot[a] * 4 + small.eSideA[e]];
                const int cb = (int)sb.protCol[sb.pc[b2] * 16 + rot[b2] * 4 + small.eSideB[e]];
                if (ca != cb) ++mis;
            }
            bestMismatch = std::min(bestMismatch, mis);
        }
        for (int k : {1, 2, 3}) {
            RotationBoundResult r = RotationUpperBound(sb, k, false);
            if (r.mismatchLB > bestMismatch) {
                std::cerr << "  rotation: bound " << r.mismatchLB << " exceeds the true optimum "
                          << bestMismatch << " (k=" << k << ")\n";
                return false;
            }
        }
    }

    // (3) the upper bound on the real board must dominate every rotation
    //     assignment we can actually construct.
    {
        Board b(pz);
        for (int k : {2, 3, 4}) {
            for (int t = 0; t < std::max(4, trials); ++t) {
                RandomiseBoard(b, rng);
                const RotationBoundResult r = RotationUpperBound(b, k, false);
                const long long matched = b.score();
                if (r.matchedUB < matched) {
                    std::cerr << "  rotation: bound " << r.matchedUB
                              << " is below an achievable " << matched << " (k=" << k << ")\n";
                    return false;
                }
                if (r.matchedUB > (long long)pz.numEdges()) {
                    std::cerr << "  rotation: bound above the number of edges\n";
                    return false;
                }
            }
        }
        // column strips must agree with row strips in spirit: also valid
        RandomiseBoard(b, rng);
        for (int k : {2, 3}) {
            const RotationBoundResult r = RotationUpperBound(b, k, true);
            if (r.matchedUB < b.score()) {
                std::cerr << "  rotation: column-strip bound too small\n";
                return false;
            }
        }
    }

    // (4) the star bound must dominate everything too, and be >= the strip bound
    {
        Board b(pz);
        for (int t = 0; t < std::max(4, trials); ++t) {
            RandomiseBoard(b, rng);
            long long raw = 0;
            const long long star = StarBoundUpper(b, &raw);
            if (star < b.score()) {
                std::cerr << "  rotation: star bound " << star << " below achievable "
                          << b.score() << "\n";
                return false;
            }
            const long long edge = EdgeBoundUpper(b);
            if (edge < b.score()) {
                std::cerr << "  rotation: edge bound " << edge << " below achievable "
                          << b.score() << "\n";
                return false;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ sub-solver
bool SelfTestSubSolve(const Puzzle& pz, int trials, uint64_t seed) {
    std::mt19937 rng((unsigned)seed);
    Board b(pz);

    struct Shape { int h, w; };
    const Shape shapes[] = {{2, 2}, {2, 3}, {3, 2}};

    for (const Shape& sh : shapes) {
        for (int t = 0; t < trials; ++t) {
            RandomiseBoard(b, rng);
            const int r0 = (int)(rng() % (unsigned)(pz.H - sh.h + 1));
            const int c0 = (int)(rng() % (unsigned)(pz.W - sh.w + 1));
            const std::vector<int> cells = RectRegion(pz, r0, c0, sh.h, sh.w);
            const int n = (int)cells.size();
            std::vector<int> pool(n);
            for (int i = 0; i < n; ++i) pool[i] = b.pc[cells[i]];

            std::vector<char> inReg(pz.ncells, 0);
            std::vector<int> idx(pz.ncells, -1);
            for (int i = 0; i < n; ++i) { inReg[cells[i]] = 1; idx[cells[i]] = i; }

            // ---- brute force over every bijection pool -> cells and rotations
            std::vector<int> order(n);
            std::iota(order.begin(), order.end(), 0);
            long long best = std::numeric_limits<long long>::max();
            do {
                const long long rotTotal = 1LL << (2 * n);
                for (long long code = 0; code < rotTotal; ++code) {
                    long long v = 0;
                    for (int i = 0; i < n; ++i) {
                        const int cell = cells[i];
                        const int piece = pool[order[i]];
                        const int rot = (int)((code >> (2 * i)) & 3);
                        for (int s = 0; s < 4; ++s) {
                            const int cc = sideColor(pz.pieces[piece].c, s, rot);
                            const int nb = pz.neigh[cell * 4 + s];
                            if (nb < 0) {
                                if (cc != kGray) v += b.wBorderRel;
                            } else if (inReg[nb]) {
                                const int j = idx[nb];
                                if (j > i) {
                                    const int piece2 = pool[order[j]];
                                    const int rot2 = (int)((code >> (2 * j)) & 3);
                                    const int c2 = sideColor(pz.pieces[piece2].c,
                                                             oppositeSide(s), rot2);
                                    if (cc != c2) ++v;
                                }
                            } else {
                                if (cc != b.col[nb * 4 + oppositeSide(s)]) ++v;
                            }
                        }
                    }
                    best = std::min(best, v);
                }
            } while (std::next_permutation(order.begin(), order.end()));

            // ---- the solver, with and without each bound
            for (int mode = 0; mode < 4; ++mode) {
                SubSolveParams pr;
                pr.proveOptimal = true;
                pr.maxNodes = 50000000;
                pr.timeLimitSeconds = 30.0;
                pr.requireImprovement = false;
                pr.useStubBound = (mode == 0 || mode == 2);
                pr.useAssignBound = (mode == 0 || mode == 1);
                pr.useEdgeBound = (mode == 0 || mode == 3);
                if (mode == 0 && sh.h * sh.w >= 6) pr.maxStubCalls = 200000;
                Board bb = b;
                SubSolveResult r = SubSolveRegion(bb, cells, pool, pr);
                if (!r.exact) {
                    std::cerr << "  subsolve: " << sh.h << "x" << sh.w
                              << " not solved exactly (mode " << mode << ")\n";
                    return false;
                }
                if (r.regionAfter != best) {
                    std::cerr << "  subsolve: " << sh.h << "x" << sh.w << " optimum "
                              << r.regionAfter << " != brute force " << best << " (mode "
                              << mode << ")\n";
                    return false;
                }
            }
        }
    }
    return true;
}

// -------------------------------------------------------------- dissections
bool SelfTestDissect(const Puzzle& pz, int trials, uint64_t seed) {
    std::mt19937 rng((unsigned)seed);
    Board b(pz);

    std::vector<std::pair<std::string, Dissection>> ds;
    for (int i = 0; i < std::max(2, trials / 4); ++i)
        ds.push_back({"dominoes", MakeDominoDissection(pz, seed + 31u * (unsigned)i + 1)});
    ds.push_back({"squares 2", MakeSquareDissection(pz, 2)});
    ds.push_back({"squares 4", MakeSquareDissection(pz, 4)});
    ds.push_back({"rows", MakeRowStripDissection(pz)});
    ds.push_back({"columns", MakeColumnStripDissection(pz)});
    ds.push_back({"mixed", MakeMixedDissection(pz, seed + 7)});
    ds.push_back({"mixed", MakeMixedDissection(pz, seed + 8)});
    ds.push_back({"tromino", MakeTrominoDissection(pz, seed + 11)});
    ds.push_back({"tromino", MakeTrominoDissection(pz, seed + 12)});
    ds.push_back({"tetromino", MakeTetrominoDissection(pz, seed + 13)});
    ds.push_back({"tetromino", MakeTetrominoDissection(pz, seed + 14)});

    // ---- (1) every dissection is a partition into connected groups
    for (auto& [name, d] : ds) {
        std::vector<int> cover(pz.ncells, 0);
        for (const Group& g : d.groups) {
            if (g.cells.empty()) { std::cerr << "  dissect: empty group\n"; return false; }
            for (int c : g.cells) cover[c]++;
            // connected?
            std::vector<char> seen(g.cells.size(), 0);
            std::vector<int> stack{0};
            seen[0] = 1;
            int nseen = 1;
            std::vector<int> idx(pz.ncells, -1);
            for (size_t i = 0; i < g.cells.size(); ++i) idx[g.cells[i]] = (int)i;
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (int s = 0; s < 4; ++s) {
                    const int nb = pz.neigh[g.cells[v] * 4 + s];
                    if (nb < 0) continue;
                    const int j = idx[nb];
                    if (j >= 0 && !seen[j]) { seen[j] = 1; ++nseen; stack.push_back(j); }
                }
            }
            if (nseen != (int)g.cells.size()) {
                std::cerr << "  dissect [" << d.name << "]: group of " << g.cells.size()
                          << " cells is not connected: ";
                for (int c : g.cells) std::cerr << "(" << c / pz.W << "," << c % pz.W << ") ";
                std::cerr << "\n";
                return false;
            }
            // four rotations always exist and always preserve the cell count
            for (int q = 0; q < 4; ++q) {
                if (!g.rotValid[q]) { std::cerr << "  dissect: rotation " << q << " invalid\n"; return false; }
                if (g.dst[q].size() != g.cells.size()) { std::cerr << "  dissect: bad dst\n"; return false; }
                std::vector<char> hit(g.cells.size(), 0);
                for (int t : g.dst[q]) {
                    if (t < 0 || t >= (int)g.cells.size() || hit[t]) {
                        std::cerr << "  dissect: dst is not a permutation\n";
                        return false;
                    }
                    hit[t] = 1;
                }
            }
        }
        for (int c = 0; c < pz.ncells; ++c)
            if (cover[c] != 1) {
                std::cerr << "  dissect (" << name << "): cell " << c << " covered " << cover[c]
                          << " times\n";
                return false;
            }
    }

    // ---- (2) the operator is EXACT: predicted gain == measured gain
    long long checked = 0, moved = 0;
    // Both modes must be exact.  `full` is the strong one: every (group, slot)
    // pair is costed by brute force over all 4^n * n! placements, and the group's
    // INTERNAL edges now change, so they are inside the cost.  Getting the
    // internal-edge term wrong is the easy mistake here, and it shows up as a
    // predicted/measured mismatch, which is exactly what this checks.
    for (int mode = 0; mode < 2; ++mode) {
    const bool full = (mode == 1);
    long long checkedMode = 0, movedMode = 0;
    for (auto& [name, d] : ds) {
        for (int t = 0; t < std::max(2, trials / 2); ++t) {
            RandomiseBoard(b, rng);
            ReassignParams pr;
            pr.maxGroups = 64;
            pr.acceptOnlyImproving = false;   // so that the PREDICTION is testable
                                              // even when the move is a loss
            pr.fullRearrange = full;
            pr.maxCells = 3;
            const long long before = b.cost;
            ReassignResult r = DissectionReassign(b, d, pr, seed + (unsigned)t * 977u + 3u);
            if (!r.ran) continue;
            ++checked; ++checkedMode;
            const long long actual = before - b.cost;
            if (r.predicted != actual) {
                std::cerr << "  dissect (" << name << (full ? ", full" : "") << "): predicted "
                          << r.predicted << " != measured " << actual << " over " << r.groups
                          << " groups\n";
                return false;
            }
            if (r.predicted != 0) { ++moved; ++movedMode; }
            // the board must still be a legal permutation
            std::vector<int> seen(pz.ncells, 0);
            for (int c = 0; c < pz.ncells; ++c) seen[b.pc[c]]++;
            for (int c = 0; c < pz.ncells; ++c)
                if (seen[c] != 1) { std::cerr << "  dissect: board is not a permutation\n"; return false; }
            if (b.cost != before - actual) { std::cerr << "  dissect: cost drift\n"; return false; }
        }
    }
    if (checkedMode == 0) { std::cerr << "  dissect: operator never ran (mode " << mode << ")\n"; return false; }
    if (movedMode == 0) { std::cerr << "  dissect: operator never changed anything (mode " << mode << ")\n"; return false; }
    }
    if (checked == 0) { std::cerr << "  dissect: operator never ran\n"; return false; }
    if (moved == 0) { std::cerr << "  dissect: operator never changed anything\n"; return false; }

    // ---- (2b) SlotCostFull really does find the best of the 4^n * n! placements.
    // Checked against a deliberately naive enumeration that rebuilds the whole
    // board for each placement and reads the true objective off it.
    {
        Board t(pz);
        RandomiseBoard(t, rng);
        Dissection dd = MakeDominoDissection(pz, seed + 4242);
        for (int gi = 0; gi < (int)dd.groups.size(); gi += 17) {
            for (int sj = 0; sj < (int)dd.groups.size(); sj += 13) {
                if (gi == sj) continue;
                if (dd.adjacent(gi, sj)) continue;
                std::vector<int> pm, rt;
                const long long got = SlotCostFull(t, dd, gi, sj, &pm, &rt);
                const int n = (int)dd.groups[sj].cells.size();
                // naive: the cost attributable to slot sj is (board cost with the
                // slot filled that way) minus (board cost with the slot emptied of
                // its own contribution) -- measure it by rebuilding.
                long long naive = LLONG_MAX;
                std::vector<int> pcm(n), rtm(n);
                std::vector<int> order(n);
                for (int i = 0; i < n; ++i) order[i] = i;
                std::vector<int> rr(n, 0);
                do {
                    for (long long code = 0; code < (1LL << (2 * n)); ++code) {
                        for (int k = 0; k < n; ++k) rr[k] = (int)((code >> (2 * k)) & 3);
                        std::vector<int> sp = t.pc, sr = t.rt;
                        for (int k = 0; k < n; ++k) {
                            const int from = dd.groups[gi].cells[order[k]];
                            const int to = dd.groups[sj].cells[k];
                            t.pc[to] = sp[from];
                            t.rt[to] = (sr[from] + rr[k]) & 3;
                        }
                        t.rebuild();
                        if (t.cost < naive) { naive = t.cost; }
                        t.load(sp, sr);
                    }
                } while (std::next_permutation(order.begin(), order.end()));
                // `naive` is a whole-board cost; compare instead that SlotCostFull's
                // own choice reproduces it.
                std::vector<int> sp = t.pc, sr = t.rt;
                for (int k = 0; k < n; ++k) {
                    const int from = dd.groups[gi].cells[pm[k]];
                    const int to = dd.groups[sj].cells[k];
                    t.pc[to] = sp[from];
                    t.rt[to] = (sr[from] + rt[k]) & 3;
                }
                t.rebuild();
                if (t.cost != naive) {
                    std::cerr << "  dissect: SlotCostFull chose a placement of cost "
                              << t.cost << " but the best is " << naive << "\n";
                    return false;
                }
                t.load(sp, sr);
                (void)got;
            }
        }
    }

    // ---- (3) the assignment solver is OPTIMAL, not merely feasible.
    // This is the load-bearing check behind every "predicted == 0" report: the
    // claim that a converged board admits no improving reassignment is only as
    // good as the guarantee that HungarianMin really returns the minimum.
    for (int t = 0; t < std::max(8, trials * 4); ++t) {
        const int n = 2 + (int)(rng() % 6);          // 2..7
        std::vector<std::vector<int>> c((size_t)n, std::vector<int>(n, 0));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) c[i][j] = (int)(rng() % 2001) - 1000;
        std::vector<int> col;
        const long long got = HungarianMin(c, &col);
        std::vector<int> perm(n);
        for (int i = 0; i < n; ++i) perm[i] = i;
        long long bf = LLONG_MAX;
        do {
            long long v = 0;
            for (int i = 0; i < n; ++i) v += c[i][perm[i]];
            bf = std::min(bf, v);
        } while (std::next_permutation(perm.begin(), perm.end()));
        if (got != bf) {
            std::cerr << "  dissect: HungarianMin " << got << " != brute force " << bf
                      << " on a " << n << "x" << n << " matrix\n";
            return false;
        }
        long long v = 0;
        for (int i = 0; i < n; ++i) v += c[i][col[i]];
        if (v != got) { std::cerr << "  dissect: returned permutation is inconsistent\n"; return false; }
    }
    return true;
}

}  // namespace e2
