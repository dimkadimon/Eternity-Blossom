// Eternity-Blossom -- coarse-grained ("chunk") moves.  See coarse.hpp.
// SPDX-License-Identifier: MIT

#include "e2/coarse.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <random>

namespace e2 {

namespace {

// --------------------------------------------------------------- rotation maps
void FillRotationMaps(Block& bl) {
    const int n = (int)bl.cells.size();
    bl.nrot = (bl.kind == kSquare) ? 4 : 2;
    bl.dst.assign(bl.nrot, std::vector<int>(n, 0));
    bl.inv.assign(bl.nrot, std::vector<int>(n, 0));
    bl.inc.assign(bl.nrot, 0);

    if (bl.kind == kSquare) {
        const int s = bl.side;
        for (int q = 0; q < 4; ++q) {
            bl.inc[q] = q;
            for (int dr = 0; dr < s; ++dr) {
                for (int dc = 0; dc < s; ++dc) {
                    // (dr,dc) -> (dc, s-1-dr), applied q times: a clockwise
                    // rotation of the block about its centre.
                    int y = dr, x = dc;
                    for (int t = 0; t < q; ++t) {
                        const int ny = x, nx = s - 1 - y;
                        y = ny;
                        x = nx;
                    }
                    bl.dst[q][dr * s + dc] = y * s + x;
                }
            }
        }
    } else {
        // A domino has a single non-trivial rigid motion: the 180 degree turn
        // about its centre, which exchanges its two cells and half-turns both
        // pieces.  (The "centre" is a lattice corner, not a cell centre, so the
        // square formula above does not apply.)
        bl.inc[0] = 0;
        bl.dst[0][0] = 0;
        bl.dst[0][1] = 1;
        bl.inc[1] = 2;
        bl.dst[1][0] = 1;
        bl.dst[1][1] = 0;
    }
    for (int q = 0; q < bl.nrot; ++q)
        for (int i = 0; i < n; ++i) bl.inv[q][bl.dst[q][i]] = i;
}

BlockSet BuildBlockSet(const Puzzle& pz, std::vector<Block> blocks) {
    BlockSet bs;
    bs.blocks = std::move(blocks);
    const int nb = (int)bs.blocks.size();
    bs.blockOf.assign(pz.ncells, -1);
    for (int i = 0; i < nb; ++i)
        for (int c : bs.blocks[i].cells) bs.blockOf[c] = i;

    bs.adj.assign(nb, {});
    bs.bslots.assign(nb, {});
    for (int i = 0; i < nb; ++i) {
        const Block& bl = bs.blocks[i];
        for (size_t ii = 0; ii < bl.cells.size(); ++ii) {
            const int cell = bl.cells[ii];
            for (int s = 0; s < 4; ++s) {
                const int nbCell = pz.neigh[cell * 4 + s];
                if (nbCell < 0) {
                    bs.bslots[i].push_back((int)(ii * 4 + s));  // border slot
                    continue;
                }
                const int j = bs.blockOf[nbCell];
                if (j == i) continue;  // edge internal to the block: invariant
                bs.bslots[i].push_back((int)(ii * 4 + s));
                if (j >= 0) {
                    std::vector<int>& a = bs.adj[i];
                    if (std::find(a.begin(), a.end(), j) == a.end()) a.push_back(j);
                }
            }
        }
        std::sort(bs.adj[i].begin(), bs.adj[i].end());
    }
    return bs;
}

// Contents of block `bi` after a rotation by q, written into np/nr (indexed by
// position within the block).
void RotatedContents(const Board& b, const BlockSet& bs, int bi, int q,
                     std::vector<int>& np, std::vector<int>& nr) {
    const Block& bl = bs.blocks[bi];
    const int n = (int)bl.cells.size();
    np.resize(n);
    nr.resize(n);
    for (int j = 0; j < n; ++j) {
        const int i = bl.inv[q][j];  // content of j comes from i
        const int cell = bl.cells[i];
        np[j] = b.pc[cell];
        nr[j] = (b.rt[cell] + bl.inc[q]) & 3;
    }
}

}  // namespace

// ------------------------------------------------------------------ dissection
BlockSet MakeSquareBlocks(const Puzzle& pz, int side, int phaseR, int phaseC) {
    std::vector<Block> bl;
    if (side < 1) return BuildBlockSet(pz, std::move(bl));
    for (int r = phaseR; r + side <= pz.H; r += side) {
        for (int c = phaseC; c + side <= pz.W; c += side) {
            Block t;
            t.kind = kSquare;
            t.side = side;
            t.r0 = r;
            t.c0 = c;
            t.shapeId = 100 + side;
            t.cells.assign((size_t)side * side, 0);
            for (int dr = 0; dr < side; ++dr)
                for (int dc = 0; dc < side; ++dc)
                    t.cells[dr * side + dc] = (r + dr) * pz.W + (c + dc);
            FillRotationMaps(t);
            bl.push_back(std::move(t));
        }
    }
    return BuildBlockSet(pz, std::move(bl));
}

BlockSet MakeDominoTiling(const Puzzle& pz, uint64_t seed, int flips) {
    const int W = pz.W, H = pz.H;
    const int n = W * H;
    std::vector<int> mate(n, -1);
    // Start from the all-horizontal tiling (requires W even).
    for (int r = 0; r < H; ++r)
        for (int c = 0; c + 1 < W; c += 2) {
            const int a = r * W + c, b = r * W + c + 1;
            mate[a] = b;
            mate[b] = a;
        }
    std::mt19937_64 rng(seed);
    for (int t = 0; t < flips; ++t) {
        const int r = (int)(rng() % (uint64_t)(H - 1));
        const int c = (int)(rng() % (uint64_t)(W - 1));
        const int a = r * W + c, b = r * W + c + 1;
        const int d = (r + 1) * W + c, e = (r + 1) * W + c + 1;
        if (mate[a] == b && mate[d] == e) {          // two horizontal -> two vertical
            mate[a] = d; mate[d] = a;
            mate[b] = e; mate[e] = b;
        } else if (mate[a] == d && mate[b] == e) {   // two vertical -> two horizontal
            mate[a] = b; mate[b] = a;
            mate[d] = e; mate[e] = d;
        }
    }
    std::vector<Block> bl;
    bl.reserve(n / 2);
    for (int a = 0; a < n; ++a) {
        const int b = mate[a];
        if (b < a) continue;
        Block t;
        const bool horiz = (b == a + 1) && (a / W == b / W);
        t.kind = horiz ? kDominoH : kDominoV;
        t.side = 2;
        t.shapeId = horiz ? 0 : 1;
        t.r0 = a / W;
        t.c0 = a % W;
        t.cells = {std::min(a, b), std::max(a, b)};
        FillRotationMaps(t);
        bl.push_back(std::move(t));
    }
    return BuildBlockSet(pz, std::move(bl));
}

// ---------------------------------------------------------------------- deltas
int BlockDelta(const Board& b, const BlockSet& bs, int bi,
               const int* newPiece, const int* newRot) {
    const Block& bl = bs.blocks[bi];
    long long d = 0;
    for (int code : bs.bslots[bi]) {
        const int i = code >> 2, s = code & 3;
        const int base = bl.cells[i] * 4 + s;
        const int nb = b.pz->neigh[base];
        // An internal edge is seen from both endpoints, so one mismatched
        // internal edge is worth 2.  A border slot is worth wBorderSlot.
        const int w = (nb < 0) ? b.wBorderSlot : 2;
        const int oldv = (b.col[base] != b.target[base]) ? 1 : 0;
        const int nc = (int)b.protCol[newPiece[i] * 16 + newRot[i] * 4 + s];
        const int newv = (nc != (int)b.target[base]) ? 1 : 0;
        d += (long long)w * (newv - oldv);
    }
    return (int)d;
}

int BlockRotateDelta(const Board& b, const BlockSet& bs, int bi, int q) {
    std::vector<int> np, nr;
    RotatedContents(b, bs, bi, q, np, nr);
    return BlockDelta(b, bs, bi, np.data(), nr.data());
}

int BlockSwapDelta(const Board& b, const BlockSet& bs, int ai, int bi, int qa, int qb) {
    if (bs.adjacent(ai, bi)) return 0;  // gains would not be additive
    const Block& A = bs.blocks[ai];
    const Block& B = bs.blocks[bi];
    if (A.cells.size() != B.cells.size()) return 0;
    const int n = (int)A.cells.size();
    std::vector<int> npA(n), nrA(n), npB(n), nrB(n);
    for (int j = 0; j < n; ++j) {
        const int ia = A.inv[qa][j];  // A's new cell j takes B's old cell ia
        npA[j] = b.pc[B.cells[ia]];
        nrA[j] = (b.rt[B.cells[ia]] + A.inc[qa]) & 3;
        const int ib = B.inv[qb][j];
        npB[j] = b.pc[A.cells[ib]];
        nrB[j] = (b.rt[A.cells[ib]] + B.inc[qb]) & 3;
    }
    return BlockDelta(b, bs, ai, npA.data(), nrA.data()) +
           BlockDelta(b, bs, bi, npB.data(), nrB.data());
}

// ------------------------------------------------------------------- applying
void ApplyBlockRotate(Board& b, const BlockSet& bs, int bi, int q) {
    std::vector<int> np, nr;
    RotatedContents(b, bs, bi, q, np, nr);
    const Block& bl = bs.blocks[bi];
    for (size_t i = 0; i < bl.cells.size(); ++i) {
        b.pc[bl.cells[i]] = np[i];
        b.rt[bl.cells[i]] = nr[i];
    }
    b.rebuild();
}

void ApplyBlockSwap(Board& b, const BlockSet& bs, int ai, int bi, int qa, int qb) {
    const Block& A = bs.blocks[ai];
    const Block& B = bs.blocks[bi];
    const int n = (int)A.cells.size();
    std::vector<int> npA(n), nrA(n), npB(n), nrB(n);
    for (int j = 0; j < n; ++j) {
        const int ia = A.inv[qa][j];
        npA[j] = b.pc[B.cells[ia]];
        nrA[j] = (b.rt[B.cells[ia]] + A.inc[qa]) & 3;
        const int ib = B.inv[qb][j];
        npB[j] = b.pc[A.cells[ib]];
        nrB[j] = (b.rt[A.cells[ib]] + B.inc[qb]) & 3;
    }
    for (int j = 0; j < n; ++j) {
        b.pc[A.cells[j]] = npA[j];
        b.rt[A.cells[j]] = nrA[j];
        b.pc[B.cells[j]] = npB[j];
        b.rt[B.cells[j]] = nrB[j];
    }
    b.rebuild();
}

// ------------------------------------------------------------------ operators
CoarseRotateResult CoarseRotateSweep(Board& b, const BlockSet& bs, bool acceptEqual) {
    CoarseRotateResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const int nb = (int)bs.blocks.size();
    std::vector<int> np, nr;
    for (int bi = 0; bi < nb; ++bi) {
        const Block& bl = bs.blocks[bi];
        int bestQ = -1, bestD = 0;
        for (int q = 1; q < bl.nrot; ++q) {
            const int d = BlockRotateDelta(b, bs, bi, q);
            ++res.tried;
            if (d < bestD || (acceptEqual && d == 0 && bestQ < 0)) {
                bestD = d;
                bestQ = q;
            }
        }
        if (bestQ >= 0 && (bestD < 0 || (acceptEqual && bestD == 0))) {
            ApplyBlockRotate(b, bs, bi, bestQ);
            res.gain += bestD;
            ++res.applied;
        }
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

CoarseSwapResult CoarseSwapPass(Board& b, const BlockSet& bs, const CoarseSwapParams& pr,
                                MatchingStats* ms) {
    CoarseSwapResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const int nb = (int)bs.blocks.size();
    if (nb < 2) return res;
    res.ran = true;
    res.costBefore = b.cost;

    // -- candidate blocks: those whose boundary is not already perfect.  A block
    //    with no violated boundary slot cannot contribute a negative delta.
    std::vector<int> cand;
    for (int i = 0; i < nb; ++i) {
        bool bad = false;
        for (int code : bs.bslots[i]) {
            const int idx = code >> 2, s = code & 3;
            const int base = bs.blocks[i].cells[idx] * 4 + s;
            if (b.col[base] != b.target[base]) { bad = true; break; }
        }
        if (bad) cand.push_back(i);
    }
    if ((int)cand.size() > pr.maxBlocks) {
        // keep the most violated blocks
        std::vector<int> viol(nb, 0);
        for (int i : cand) {
            int v = 0;
            for (int code : bs.bslots[i]) {
                const int idx = code >> 2, s = code & 3;
                const int base = bs.blocks[i].cells[idx] * 4 + s;
                if (b.col[base] != b.target[base]) v += (b.pz->neigh[base] < 0) ? b.wBorderSlot : 2;
            }
            viol[i] = v;
        }
        std::stable_sort(cand.begin(), cand.end(),
                         [&](int x, int y) { return viol[x] > viol[y]; });
        cand.resize(pr.maxBlocks);
        std::sort(cand.begin(), cand.end());
    }
    const int m = (int)cand.size();
    res.blocks = m;
    if (m < 2) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // -- rotation variants to try per pair
    std::vector<std::pair<int, int>> variants;
    {
        const int nrot = bs.blocks[cand[0]].nrot;
        if (pr.rotVariants <= 1 || nrot <= 1) {
            variants.push_back({0, 0});
        } else if (pr.rotVariants <= 4) {
            for (int q = 0; q < nrot; ++q) variants.push_back({q, q});
        } else {
            for (int qa = 0; qa < nrot; ++qa)
                for (int qb = 0; qb < nrot; ++qb) variants.push_back({qa, qb});
        }
    }

    // -- candidate swaps: non-adjacent, same shape, positive gain
    std::vector<WEdge> edges;
    struct Chosen { int qa, qb; };
    std::vector<Chosen> chosenRot;
    for (int i = 0; i < m && (int)edges.size() < pr.maxPairs; ++i) {
        for (int j = i + 1; j < m && (int)edges.size() < pr.maxPairs; ++j) {
            const int A = cand[i], B = cand[j];
            if (bs.blocks[A].shapeId != bs.blocks[B].shapeId) continue;
            if (bs.adjacent(A, B)) continue;  // the complement graph
            int bd = 0, bqa = 0, bqb = 0;
            for (const auto& v : variants) {
                const int d = BlockSwapDelta(b, bs, A, B, v.first, v.second);
                if (d < bd) { bd = d; bqa = v.first; bqb = v.second; }
            }
            if (bd < 0) {
                edges.emplace_back(i, j, -bd);
                chosenRot.push_back({bqa, bqb});
            }
        }
    }
    res.pairsOffered = (int)edges.size();
    if (edges.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // -- maximum-weight matching on the complement graph (non-bipartite)
    std::vector<std::pair<int, int>> pairs;
    long long w = 0;
    if (!MaxWeightMatching(m, edges, pairs, w, ms)) {
        res.error = "matching solver failed";
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.pairsChosen = (int)pairs.size();
    res.predictedGain = w;
    if (pairs.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // -- recover each pair's gain/rotations and sort by decreasing gain
    std::vector<std::array<int, 5>> chosen;  // {gain, i, j, qa, qb}
    for (size_t e = 0; e < edges.size(); ++e) {
        chosen.push_back({std::get<2>(edges[e]), std::get<0>(edges[e]), std::get<1>(edges[e]),
                          chosenRot[e].qa, chosenRot[e].qb});
    }
    std::sort(chosen.begin(), chosen.end(),
              [](const std::array<int, 5>& x, const std::array<int, 5>& y) { return x[0] > y[0]; });

    // -- greedy filter: two swaps may be applied together only if no block of
    //    one touches a block of the other.
    std::vector<char> taken(m, 0);
    std::vector<std::array<int, 5>> applyList;
    for (const auto& c : chosen) {
        const int bi = cand[c[1]], bj = cand[c[2]];
        if (taken[c[1]] || taken[c[2]]) continue;
        bool clash = false;
        for (const auto& a : applyList) {
            const int ai = cand[a[1]], aj = cand[a[2]];
            if (bs.adjacent(bi, ai) || bs.adjacent(bi, aj) ||
                bs.adjacent(bj, ai) || bs.adjacent(bj, aj)) { clash = true; break; }
        }
        if (clash) continue;
        taken[c[1]] = 1;
        taken[c[2]] = 1;
        applyList.push_back(c);
    }
    res.pairsApplied = (int)applyList.size();
    if (applyList.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    const long long savedCost = b.cost;
    long long predicted = 0;
    for (const auto& c : applyList) {
        predicted += c[0];
        ApplyBlockSwap(b, bs, cand[c[1]], cand[c[2]], c[3], c[4]);
    }
    res.costAfter = b.cost;
    res.actualGain = savedCost - b.cost;
    res.predictedGain = predicted;
    if (pr.acceptOnlyImproving && b.cost >= savedCost) {
        b.load(savedPc, savedRt);
        res.costAfter = savedCost;
        res.actualGain = 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace e2
