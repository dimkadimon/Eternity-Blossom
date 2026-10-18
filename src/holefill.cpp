// Eternity-Blossom -- exact completion of a PARTIAL board (hole filling).
//
// SPDX-License-Identifier: MIT
//
// See include/e2/holefill.hpp for what this solves and why the bipartite
// assignment in `complete` is only an approximation of it.

#include "e2/holefill.hpp"

#include <algorithm>
#include <climits>
#include <array>

namespace e2 {
namespace {

// Penalty per non-grey border slot.  Large enough that a fill which breaks the
// border is never preferred, small enough that an infeasible sub-problem still
// returns *something* rather than throwing.
constexpr long long kBorderPenalty = 1000;

struct Solver {
    const Puzzle& pz;
    const std::vector<int>& pc;
    const std::vector<int>& rt;
    int m = 0;
    std::vector<int> holes;         // cell ids, ascending
    std::vector<int> unused;        // piece ids
    std::vector<int> fixedGain;     // [(hi*m + pi)*4 + rot]
    std::vector<int> maxFixed;      // [hi*m + pi]  max over rotations
    std::vector<char> isHole;
    std::vector<int> holeIdx;       // cell -> hole index, -1 if not a hole
    std::vector<int> order;         // position -> hole index
    std::vector<int> hhRem;         // position -> undecided hole-hole edges
    std::vector<int> eStart;        // position -> offset into eFlat (size m+1)
    std::vector<int> eFlat;         // (otherHi, otherSide, mySide) triples

    long long best = LLONG_MIN;
    std::vector<int> bestPi, bestRt;   // by hole index
    std::vector<int> curPi, curRt;     // by hole index, -1 while unplaced
    long long nodes = 0;
    long long nodeLimit = 0;
    bool over = false;

    Solver(const Puzzle& p, const std::vector<int>& boardPc, const std::vector<int>& boardRt)
        : pz(p), pc(boardPc), rt(boardRt) {}

    int colorOf(int piece, int rot, int side) const {
        return pz.pieces[piece].c[(side + 4 - (rot & 3)) & 3];
    }
    int boardColor(int cell, int side) const {
        return colorOf(pc[cell], rt[cell], side);
    }

    void build() {
        isHole.assign(pz.ncells, 0);
        holeIdx.assign(pz.ncells, -1);
        for (int i = 0; i < m; ++i) { isHole[holes[i]] = 1; holeIdx[holes[i]] = i; }

        // fixedGain: what a hole earns from its ALREADY FILLED neighbours.
        fixedGain.assign((size_t)m * m * 4, 0);
        maxFixed.assign((size_t)m * m, INT_MIN);
        for (int hi = 0; hi < m; ++hi) {
            const int cell = holes[hi];
            for (int pi = 0; pi < m; ++pi) {
                const int piece = unused[pi];
                int best4 = INT_MIN;
                for (int rot = 0; rot < 4; ++rot) {
                    long long g = 0;
                    for (int s = 0; s < 4; ++s) {
                        const int nb = pz.neigh[cell * 4 + s];
                        const int mine = colorOf(piece, rot, s);
                        if (nb < 0) {
                            if (mine != kGray) g -= kBorderPenalty;
                        } else if (!isHole[nb]) {
                            if (mine == boardColor(nb, oppositeSide(s))) ++g;
                        }
                    }
                    fixedGain[((size_t)hi * m + pi) * 4 + rot] = (int)g;
                    best4 = std::max(best4, (int)g);
                }
                maxFixed[(size_t)hi * m + pi] = best4;
            }
        }

        // hole-hole edges, once each: (hi, hj, side_i, side_j)
        std::vector<std::array<int, 4>> edges;
        for (int hi = 0; hi < m; ++hi) {
            const int cell = holes[hi];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                if (nb < 0 || !isHole[nb]) continue;
                const int hj = holeIdx[nb];
                if (hj > hi) edges.push_back({hi, hj, s, oppositeSide(s)});
            }
        }

        // Order the holes greedily: always take the one with the most
        // neighbours already ordered.  Keeps the frontier -- and so the number
        // of hole-hole edges that are still undecided -- small.
        std::vector<int> pos(m, -1);
        std::vector<char> taken(m, 0);
        std::vector<int> placed;
        for (int step = 0; step < m; ++step) {
            int pick = -1, pickV = -1;
            for (int h = 0; h < m; ++h) {
                if (taken[h]) continue;
                int v = 0;
                for (const auto& e : edges) {
                    if ((e[0] == h && taken[e[1]]) || (e[1] == h && taken[e[0]])) ++v;
                }
                if (v > pickV) { pickV = v; pick = h; }
            }
            taken[pick] = 1;
            pos[pick] = step;
            placed.push_back(pick);
        }
        order = placed;

        // earlier[k]: hole-hole edges from order[k] to holes earlier in order.
        std::vector<std::vector<std::array<int, 3>>> eBy(m);
        for (const auto& e : edges) {
            const int a = e[0], b = e[1], sa = e[2], sb = e[3];
            if (pos[a] < pos[b]) eBy[pos[b]].push_back({a, sa, sb});
            else eBy[pos[a]].push_back({b, sb, sa});
        }
        eStart.assign(m + 1, 0);
        for (int k = 0; k < m; ++k) eStart[k + 1] = eStart[k] + 3 * (int)eBy[k].size();
        eFlat.assign(eStart[m], 0);
        for (int k = 0; k < m; ++k) {
            int o = eStart[k];
            for (const auto& t : eBy[k]) { eFlat[o++] = t[0]; eFlat[o++] = t[1]; eFlat[o++] = t[2]; }
        }
        hhRem.assign(m + 1, 0);
        for (int k = m - 1; k >= 0; --k) {
            hhRem[k] = hhRem[k + 1] + (int)eBy[k].size();
        }
    }

    void dfs(int k, long long gain, std::vector<int>& avail) {
        if (over) return;
        if (++nodes > nodeLimit) { over = true; return; }
        if (k == m) {
            if (gain > best) { best = gain; bestPi = curPi; bestRt = curRt; }
            return;
        }
        // admissible upper bound on the final gain
        long long bound = gain + hhRem[k];
        for (int kk = k; kk < m; ++kk) {
            const int h = order[kk];
            int mx = INT_MIN;
            for (int pi : avail) mx = std::max(mx, maxFixed[(size_t)h * m + pi]);
            if (mx != INT_MIN) bound += mx;
        }
        if (bound <= best) return;

        const int hi = order[k];
        for (size_t ai = 0; ai < avail.size(); ++ai) {
            const int pi = avail[ai];
            std::swap(avail[ai], avail.back());
            avail.pop_back();
            for (int rot = 0; rot < 4; ++rot) {
                long long g = fixedGain[((size_t)hi * m + pi) * 4 + rot];
                for (int e = eStart[k]; e < eStart[k + 1]; e += 3) {
                    const int oh = eFlat[e], os = eFlat[e + 1], ms = eFlat[e + 2];
                    if (colorOf(unused[pi], rot, ms) ==
                        colorOf(unused[curPi[oh]], curRt[oh], os)) ++g;
                }
                curPi[hi] = pi;
                curRt[hi] = rot;
                dfs(k + 1, gain + g, avail);
            }
            avail.push_back(pi);
            std::swap(avail[ai], avail.back());
        }
        curPi[hi] = -1;
        curRt[hi] = 0;
    }
};

}  // namespace

long long EvalHoleFill(const Puzzle& pz, const std::vector<int>& pc, const std::vector<int>& rt,
                       const std::vector<int>& cells, const std::vector<int>& pieces,
                       const std::vector<int>& rots) {
    std::vector<int> p = pc, r = rt;
    std::vector<char> isHole(pz.ncells, 0);
    for (size_t i = 0; i < cells.size(); ++i) {
        p[cells[i]] = pieces[i];
        r[cells[i]] = rots[i];
        isHole[cells[i]] = 1;
    }
    long long gain = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        const int c = cells[i];
        for (int s = 0; s < 4; ++s) {
            const int nb = pz.neigh[c * 4 + s];
            const int mine = pz.pieces[p[c]].c[(s + 4 - (r[c] & 3)) & 3];
            if (nb < 0) {
                if (mine != kGray) gain -= kBorderPenalty;
                continue;
            }
            if (isHole[nb] && nb < c) continue;   // count hole-hole edges once
            const int theirs = pz.pieces[p[nb]].c[(oppositeSide(s) + 4 - (r[nb] & 3)) & 3];
            if (mine == theirs) ++gain;
        }
    }
    return gain;
}

HoleFillResult SolveHoleFill(const Puzzle& pz, const std::vector<int>& pc,
                             const std::vector<int>& rt,
                             const std::vector<int>& seedPiece, const std::vector<int>& seedRot,
                             long long nodeLimit) {
    HoleFillResult out;
    std::vector<int> holes, unused;
    std::vector<char> used(pz.ncells, 0);
    for (int c = 0; c < pz.ncells; ++c) {
        if (pc[c] < 0) holes.push_back(c);
        else used[pc[c]] = 1;
    }
    for (int p = 0; p < pz.ncells; ++p) if (!used[p]) unused.push_back(p);
    out.holes = (int)holes.size();
    if (holes.empty() || (int)unused.size() != out.holes) return out;

    Solver sv(pz, pc, rt);
    sv.m = (int)holes.size();
    sv.holes = holes;
    sv.unused = unused;
    sv.nodeLimit = nodeLimit;
    sv.build();

    // Seed with the caller's solution (the bipartite one) so that the search
    // starts from a real lower bound and prunes immediately.
    if ((int)seedPiece.size() == sv.m && (int)seedRot.size() == sv.m) {
        std::vector<int> seedPi(sv.m, -1);
        bool ok = true;
        for (int i = 0; i < sv.m; ++i) {
            auto it = std::find(unused.begin(), unused.end(), seedPiece[i]);
            if (it == unused.end()) { ok = false; break; }
            seedPi[i] = (int)(it - unused.begin());
        }
        if (ok) {
            sv.best = EvalHoleFill(pz, pc, rt, holes, seedPiece, seedRot);
            sv.bestPi = seedPi;
            sv.bestRt = seedRot;
        }
    }

    sv.curPi.assign(sv.m, -1);
    sv.curRt.assign(sv.m, 0);
    std::vector<int> avail(sv.m);
    for (int i = 0; i < sv.m; ++i) avail[i] = i;
    sv.dfs(0, 0, avail);

    out.cell = holes;
    out.gain = sv.best;
    out.nodes = sv.nodes;
    out.optimal = !sv.over;
    if (!sv.bestPi.empty()) {
        out.piece.resize(sv.m);
        out.rot.resize(sv.m);
        for (int i = 0; i < sv.m; ++i) {
            out.piece[i] = unused[sv.bestPi[i]];
            out.rot[i] = sv.bestRt[i];
        }
    }
    return out;
}

}  // namespace e2
