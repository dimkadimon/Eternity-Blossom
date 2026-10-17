// Eternity-Blossom -- rotation-only optimisation and its upper bound.
// See rotation.hpp.  SPDX-License-Identifier: MIT

#include "e2/rotation.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace e2 {

namespace {

constexpr int kMaxStripK = 4;  // 4^4 = 256 DP states

struct Geom {
    int W, H;
    bool cols;
    int start, K, L;
    int sLongFwd, sLongBwd, sCrossFwd, sCrossBwd;

    int cell(int j, int t) const {
        return cols ? (t * W + (start + j)) : ((start + j) * W + t);
    }
};

Geom MakeGeom(const Board& b, const StripSpec& sp) {
    Geom g;
    g.W = b.pz->W;
    g.H = b.pz->H;
    g.cols = sp.columns;
    g.start = sp.start;
    g.K = sp.k;
    g.L = g.cols ? g.H : g.W;
    if (g.cols) {
        g.sLongFwd = 2;  // S : toward row+1
        g.sLongBwd = 0;  // N : toward row-1
        g.sCrossFwd = 1; // E : toward col+1
        g.sCrossBwd = 3; // W : toward col-1
    } else {
        g.sLongFwd = 1;  // E : toward col+1
        g.sLongBwd = 3;  // W : toward col-1
        g.sCrossFwd = 2; // S : toward row+1
        g.sCrossBwd = 0; // N : toward row-1
    }
    return g;
}

}  // namespace

StripOpt OptimizeStrip(const Board& b, const StripSpec& sp, bool moveMode) {
    StripOpt out;
    const Puzzle& pz = *b.pz;
    const Geom g = MakeGeom(b, sp);
    const int K = g.K, L = g.L;
    if (K < 1 || K > kMaxStripK || L < 1) return out;

    const int intW = moveMode ? 2 : 1;              // weight of a mismatched internal edge
    const int borW = moveMode ? b.wBorderSlot : 0;  // weight of a bad border slot
    const bool ext = moveMode;                      // count edges leaving the strip?

    const int NS = 1 << (2 * K);  // 4^K

    out.cells.resize((size_t)K * L);
    std::vector<int> pcL((size_t)K * L), rtL((size_t)K * L);
    for (int j = 0; j < K; ++j) {
        for (int t = 0; t < L; ++t) {
            const int c = g.cell(j, t);
            out.cells[(size_t)j * L + t] = c;
            pcL[(size_t)j * L + t] = b.pc[c];
            rtL[(size_t)j * L + t] = b.rt[c];
        }
    }

    // Colour that piece pcL[j,t] shows on side s at rotation r.
    auto C = [&](int j, int t, int r, int s) -> int {
        return (int)b.protCol[pcL[(size_t)j * L + t] * 16 + r * 4 + s];
    };
    // Weight contributed by one slot of a strip cell, given the colour it shows.
    auto slotW = [&](int cell, int side, int colr) -> int {
        const int nb = pz.neigh[cell * 4 + side];
        if (nb < 0) return (colr != kGray) ? borW : 0;
        if (!ext) return 0;
        return (colr != (int)b.col[nb * 4 + oppositeSide(side)]) ? intW : 0;
    };

    // ---- base[t][state]: cross edges inside column t + all exterior slots
    std::vector<int> base((size_t)L * NS, 0);
    int d[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int t = 0; t < L; ++t) {
        for (int st = 0; st < NS; ++st) {
            int x = st;
            for (int j = 0; j < K; ++j) { d[j] = x & 3; x >>= 2; }
            int v = 0;
            for (int j = 0; j + 1 < K; ++j)
                if (C(j, t, d[j], g.sCrossFwd) != C(j + 1, t, d[j + 1], g.sCrossBwd)) v += intW;
            v += slotW(g.cell(0, t), g.sCrossBwd, C(0, t, d[0], g.sCrossBwd));
            v += slotW(g.cell(K - 1, t), g.sCrossFwd, C(K - 1, t, d[K - 1], g.sCrossFwd));
            if (t == 0)
                for (int j = 0; j < K; ++j)
                    v += slotW(g.cell(j, 0), g.sLongBwd, C(j, 0, d[j], g.sLongBwd));
            if (t == L - 1)
                for (int j = 0; j < K; ++j)
                    v += slotW(g.cell(j, L - 1), g.sLongFwd, C(j, L - 1, d[j], g.sLongFwd));
            base[(size_t)t * NS + st] = v;
        }
    }

    // ---- lc[t][j][rA*4+rB]: long edge from (j,t-1) to (j,t)
    std::vector<int> lc((size_t)L * K * 16, 0);
    for (int t = 1; t < L; ++t)
        for (int j = 0; j < K; ++j)
            for (int rA = 0; rA < 4; ++rA)
                for (int rB = 0; rB < 4; ++rB)
                    lc[(((size_t)t * K + j) * 16) + rA * 4 + rB] =
                        (C(j, t - 1, rA, g.sLongFwd) != C(j, t, rB, g.sLongBwd)) ? intW : 0;

    // ---- DP
    const int INF = std::numeric_limits<int>::max() / 4;
    std::vector<int> f((size_t)L * NS, 0), par((size_t)L * NS, -1);
    for (int st = 0; st < NS; ++st) f[st] = base[st];
    for (int t = 1; t < L; ++t) {
        const int* lcT = &lc[(size_t)t * K * 16];
        for (int cur = 0; cur < NS; ++cur) {
            int x = cur;
            for (int j = 0; j < K; ++j) { d[j] = x & 3; x >>= 2; }
            int best = INF, bp = -1;
            for (int prev = 0; prev < NS; ++prev) {
                int v = f[(size_t)(t - 1) * NS + prev];
                if (v >= best) continue;
                int y = prev;
                for (int j = 0; j < K; ++j) {
                    v += lcT[(size_t)j * 16 + (y & 3) * 4 + d[j]];
                    y >>= 2;
                }
                if (v < best) { best = v; bp = prev; }
            }
            f[(size_t)t * NS + cur] = best + base[(size_t)t * NS + cur];
            par[(size_t)t * NS + cur] = bp;
        }
    }

    int bestSt = 0, bestVal = INF;
    for (int st = 0; st < NS; ++st) {
        const int v = f[(size_t)(L - 1) * NS + st];
        if (v < bestVal) { bestVal = v; bestSt = st; }
    }
    out.optimum = bestVal;

    // ---- backtrack
    std::vector<int> stAt(L);
    {
        int st = bestSt;
        for (int t = L - 1; t >= 0; --t) {
            stAt[t] = st;
            st = par[(size_t)t * NS + st];
        }
    }
    out.rot.assign((size_t)K * L, 0);
    for (int t = 0; t < L; ++t) {
        int x = stAt[t];
        for (int j = 0; j < K; ++j) { out.rot[(size_t)j * L + t] = x & 3; x >>= 2; }
    }

    // ---- value of the present rotations under the same terms
    long long cur = 0;
    for (int t = 0; t < L; ++t) {
        int st = 0;
        for (int j = K - 1; j >= 0; --j) st = (st << 2) | rtL[(size_t)j * L + t];
        cur += base[(size_t)t * NS + st];
    }
    for (int t = 1; t < L; ++t)
        for (int j = 0; j < K; ++j)
            cur += lc[(((size_t)t * K + j) * 16) + rtL[(size_t)j * L + t - 1] * 4 +
                      rtL[(size_t)j * L + t]];
    out.current = cur;
    return out;
}

void ApplyStripOpt(Board& b, const StripOpt& o) {
    for (size_t i = 0; i < o.cells.size(); ++i) b.rt[o.cells[i]] = o.rot[i];
    b.rebuild();
}

StripSweepResult StripRotateSweep(Board& b, int k, bool columns) {
    StripSweepResult res;
    const auto t0 = std::chrono::steady_clock::now();
    if (k < 1 || k > kMaxStripK) return res;
    const int N = columns ? b.pz->W : b.pz->H;
    for (int s = 0; s < N; s += k) {
        const int kk = std::min(k, N - s);
        StripSpec sp{columns, s, kk};
        StripOpt o = OptimizeStrip(b, sp, true);
        ++res.strips;
        if (o.optimum < o.current) {
            res.gain += o.optimum - o.current;
            ++res.applied;
            ApplyStripOpt(b, o);
        }
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ------------------------------------------------------------------ the oracle
namespace {

// Sum over the seam edges of "can this edge EVER match".  The seam edges are
// vertex-disjoint, so the minimum of the sum is the sum of the minima.
long long SeamMinimum(const Board& b, bool columns, int start, int k1) {
    const int W = b.pz->W, H = b.pz->H;
    const int L = columns ? H : W;
    const int a0 = start + k1 - 1;  // last line of the upper strip
    const int b0 = start + k1;      // first line of the lower strip
    auto cellAt = [&](int across, int t) { return columns ? (t * W + across) : (across * W + t); };
    const int sF = columns ? 1 : 2;  // side of a0 facing b0
    const int sB = columns ? 3 : 0;  // side of b0 facing a0
    long long s = 0;
    for (int t = 0; t < L; ++t) {
        const int cA = cellAt(a0, t), cB = cellAt(b0, t);
        bool can = false;
        for (int rA = 0; rA < 4 && !can; ++rA) {
            const int ca = (int)b.protCol[b.pc[cA] * 16 + rA * 4 + sF];
            for (int rB = 0; rB < 4; ++rB) {
                const int cb = (int)b.protCol[b.pc[cB] * 16 + rB * 4 + sB];
                if (ca == cb) { can = true; break; }
            }
        }
        if (!can) ++s;
    }
    return s;
}

}  // namespace

RotationBoundResult RotationUpperBound(const Board& b, int k, bool columns) {
    RotationBoundResult res;
    const auto t0 = std::chrono::steady_clock::now();
    if (k < 1) return res;
    const int N = columns ? b.pz->W : b.pz->H;
    std::vector<int> starts;
    for (int s = 0; s < N; s += k) starts.push_back(s);

    long long stripPart = 0, seamPart = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        const int kk = (i + 1 < starts.size() ? starts[i + 1] : N) - starts[i];
        const int kc = std::min(kk, kMaxStripK);
        // A strip taller than kMaxStripK would have to be split; keep the DP
        // honest by bounding it.
        for (int off = 0; off < kk; off += kc) {
            const int h = std::min(kc, kk - off);
            StripSpec sp{columns, starts[i] + off, h};
            stripPart += OptimizeStrip(b, sp, false).optimum;
        }
        if (i + 1 < starts.size()) seamPart += SeamMinimum(b, columns, starts[i], kk);
        ++res.strips;
    }
    res.stripPart = stripPart;
    res.seamPart = seamPart;
    res.mismatchLB = stripPart + seamPart;
    res.matchedUB = (long long)b.pz->numEdges() - res.mismatchLB;
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

long long EdgeBoundUpper(const Board& b, long long* impossible) {
    const Puzzle& pz = *b.pz;
    long long bad = 0;
    for (int e = 0; e < pz.numEdges(); ++e) {
        const int a = pz.eCellA[e], sa = pz.eSideA[e];
        const int bb = pz.eCellB[e], sb = pz.eSideB[e];
        const int pa = b.pc[a], pb = b.pc[bb];
        bool can = false;
        for (int ra = 0; ra < 4 && !can; ++ra) {
            const int ca = (int)b.protCol[pa * 16 + ra * 4 + sa];
            for (int rb = 0; rb < 4; ++rb)
                if (ca == (int)b.protCol[pb * 16 + rb * 4 + sb]) { can = true; break; }
        }
        if (!can) ++bad;
    }
    if (impossible) *impossible = bad;
    return (long long)pz.numEdges() - bad;
}

long long StarBoundUpper(const Board& b, long long* rawSum) {
    const Puzzle& pz = *b.pz;
    long long sum = 0;
    for (int cell = 0; cell < pz.ncells; ++cell) {
        const int pc = b.pc[cell];
        long long best = 0;
        for (int r = 0; r < 4; ++r) {
            long long v = 0;
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                if (nb < 0) continue;
                const int ca = (int)b.protCol[pc * 16 + r * 4 + s];
                const int pb = b.pc[nb];
                const int sb = oppositeSide(s);
                bool can = false;
                for (int rb = 0; rb < 4; ++rb)
                    if (ca == (int)b.protCol[pb * 16 + rb * 4 + sb]) { can = true; break; }
                if (can) ++v;
            }
            if (v > best) best = v;
        }
        sum += best;
    }
    if (rawSum) *rawSum = sum;
    return sum / 2;  // every internal edge is counted from both of its endpoints
}

}  // namespace e2
