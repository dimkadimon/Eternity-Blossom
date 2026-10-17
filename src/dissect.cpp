// Eternity-Blossom -- dissection-based exact reassignment.  See dissect.hpp.
// SPDX-License-Identifier: MIT

#include "e2/dissect.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <random>

#include "e2/assign.hpp"


namespace e2 {
// Rotate by q quarter turns clockwise, then translate back to the origin.
//
// Working in doubled coordinates (a cell centre sits at (2i+1, 2j+1)) a quarter
// turn about the origin is (Y,X) -> (X,-Y), which maps a pair of odd numbers to
// a pair of odd numbers -- so it ALWAYS lands on cell centres again, for every
// shape and every q.  The translation that normalises the bounding box back to
// the origin is part of the rigid motion.
//
// (The mistake worth recording: demanding the rotation be about the shape's own
// centre, with no translation, makes it look as though a 1x2 domino only has
// two orientations, because its centre sits on a grid line rather than at a
// cell centre.  Rotate-and-translate has no such restriction -- a horizontal
// domino turns into a vertical one under a quarter turn.)
void RotateShapeOffsets(const std::vector<std::pair<int, int>>& in, int q,
                        std::vector<std::pair<int, int>>& out,
                        std::vector<int>& dst) {
    out.clear();
    out.reserve(in.size());
    for (auto& p : in) {
        int Y = 2 * p.first + 1, X = 2 * p.second + 1;   // doubled cell centre
        for (int t = 0; t < q; ++t) { const int nY = X, nX = -Y; Y = nY; X = nX; }
        out.push_back({(Y - 1) / 2, (X - 1) / 2});       // exact: Y and X are odd
    }
    int mr = INT_MAX, mc = INT_MAX;
    for (auto& p : out) { mr = std::min(mr, p.first); mc = std::min(mc, p.second); }
    for (auto& p : out) { p.first -= mr; p.second -= mc; }
    std::vector<std::pair<int, int>> canon = out;
    std::sort(canon.begin(), canon.end());
    dst.assign(in.size(), 0);
    for (size_t i = 0; i < out.size(); ++i)
        dst[i] = (int)(std::lower_bound(canon.begin(), canon.end(), out[i]) - canon.begin());
}


namespace {

// ------------------------------------------------------------ shape utilities
using Offs = std::vector<std::pair<int, int>>;

int BoxOf(const Offs& o, int& a, int& b) {
    int mr = 99, mc = 99, Mr = -1, Mc = -1;
    for (auto& p : o) {
        mr = std::min(mr, p.first);  Mc = std::max(Mc, p.second);
        Mr = std::max(Mr, p.first);  mc = std::min(mc, p.second);
    }
    a = Mr - mr + 1;
    b = Mc - mc + 1;
    return 0;
}

std::string ShapeKey(const Offs& o) {
    Offs s = o;
    std::sort(s.begin(), s.end());
    std::string k;
    for (auto& p : s) {
        k += std::to_string(p.first);
        k += ',';
        k += std::to_string(p.second);
        k += ';';
    }
    return k;
}


int ShapeIdOf(std::map<std::string, int>& ids, const Offs& o) {
    const std::string k = ShapeKey(o);
    auto it = ids.find(k);
    if (it != ids.end()) return it->second;
    const int id = (int)ids.size();
    ids[k] = id;
    return id;
}

// Fill in a group's rotation tables from its shape.
void BuildGroupRotations(Group& g, const Offs& offs, std::map<std::string, int>& ids) {
    g.rotValid.assign(4, 0);
    g.dst.assign(4, {});
    g.inv.assign(4, {});
    g.inc.assign(4, 0);
    g.rotShape.assign(4, g.shapeId);
    const int n = (int)offs.size();
    for (int q = 0; q < 4; ++q) {
        Offs out;
        std::vector<int> dst;
        RotateShapeOffsets(offs, q, out, dst);
        g.rotValid[q] = 1;
        g.inc[q] = q;
        g.dst[q] = dst;
        g.inv[q].assign(n, 0);
        for (int i = 0; i < n; ++i) g.inv[q][dst[i]] = i;
        g.rotShape[q] = ShapeIdOf(ids, out);
    }
}

// --------------------------------------------------------------- constructors
struct Pending {
    Offs offs;
    std::vector<int> cells;
};

Dissection Finalise(const Puzzle& pz, std::vector<Pending>& pend, const std::string& name) {
    std::map<std::string, int> ids;
    Dissection d;
    d.name = name;
    d.groupOf.assign(pz.ncells, -1);
    for (auto& p : pend) {
        // CANONICALISE.  A group's `cells` array must be listed in the same
        // order as its shape's canonical (normalised, then sorted) offset list,
        // because dst[]/inv[] are permutations expressed in canonical indices:
        // SlotCost() looks the slot's cell-index up as a canonical index and
        // maps it back to the source's placement index.  Some constructors
        // (notably the mixed L/Z dissection, whose L and S shapes carry a
        // negative column offset) hand over offsets that are neither normalised
        // nor sorted, which silently scrambles the correspondence.
        Offs offs = p.offs;
        std::vector<int> cells = p.cells;
        int mr = INT_MAX, mc = INT_MAX;
        for (auto& o : offs) { mr = std::min(mr, o.first); mc = std::min(mc, o.second); }
        for (auto& o : offs) { o.first -= mr; o.second -= mc; }
        std::vector<int> perm(offs.size());
        for (size_t i = 0; i < perm.size(); ++i) perm[i] = (int)i;
        std::sort(perm.begin(), perm.end(),
                  [&](int a, int b) { return offs[a] < offs[b]; });
        {
            Offs so; std::vector<int> sc;
            for (int i : perm) { so.push_back(offs[i]); sc.push_back(cells[i]); }
            offs.swap(so); cells.swap(sc);
        }
        Group g;
        g.cells = std::move(cells);
        g.shapeId = ShapeIdOf(ids, offs);
        BuildGroupRotations(g, offs, ids);
        d.groups.push_back(std::move(g));
    }
    const int ng = (int)d.groups.size();
    for (int i = 0; i < ng; ++i)
        for (int c : d.groups[i].cells) d.groupOf[c] = i;
    d.adj.assign(ng, {});
    d.bslots.assign(ng, {});
    for (int i = 0; i < ng; ++i) {
        for (size_t ii = 0; ii < d.groups[i].cells.size(); ++ii) {
            const int cell = d.groups[i].cells[ii];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                if (nb < 0) { d.bslots[i].push_back((int)(ii * 4 + s)); continue; }
                const int j = d.groupOf[nb];
                if (j == i) continue;  // internal to the group: invariant
                d.bslots[i].push_back((int)(ii * 4 + s));
                if (j >= 0) {
                    std::vector<int>& a = d.adj[i];
                    if (std::find(a.begin(), a.end(), j) == a.end()) a.push_back(j);
                }
            }
        }
        std::sort(d.adj[i].begin(), d.adj[i].end());
    }
    return d;
}

}  // namespace

// ---------------------------------------------------------------- dissections
Dissection MakeDominoDissection(const Puzzle& pz, uint64_t seed, int flips) {
    const int W = pz.W, H = pz.H, n = pz.ncells;
    std::vector<int> mate(n, -1);
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
        const int dd = (r + 1) * W + c, e = (r + 1) * W + c + 1;
        if (mate[a] == b && mate[dd] == e) {
            mate[a] = dd; mate[dd] = a; mate[b] = e; mate[e] = b;
        } else if (mate[a] == dd && mate[b] == e) {
            mate[a] = b; mate[b] = a; mate[dd] = e; mate[e] = dd;
        }
    }
    std::vector<Pending> pend;
    for (int a = 0; a < n; ++a) {
        const int b = mate[a];
        if (b < a) continue;
        const bool horiz = (b == a + 1) && (a / W == b / W);
        Pending p;
        p.cells = {std::min(a, b), std::max(a, b)};
        p.offs = horiz ? Offs{{0, 0}, {0, 1}} : Offs{{0, 0}, {1, 0}};
        pend.push_back(std::move(p));
    }
    return Finalise(pz, pend, "dominoes");
}

Dissection MakeSquareDissection(const Puzzle& pz, int side, int phaseR, int phaseC) {
    std::vector<Pending> pend;
    for (int r = phaseR; r + side <= pz.H; r += side)
        for (int c = phaseC; c + side <= pz.W; c += side) {
            Pending p;
            for (int dr = 0; dr < side; ++dr)
                for (int dc = 0; dc < side; ++dc) {
                    p.offs.push_back({dr, dc});
                    p.cells.push_back((r + dr) * pz.W + (c + dc));
                }
            pend.push_back(std::move(p));
        }
    return Finalise(pz, pend, "squares " + std::to_string(side));
}

Dissection MakeRowStripDissection(const Puzzle& pz) {
    std::vector<Pending> pend;
    for (int r = 0; r < pz.H; ++r) {
        Pending p;
        for (int c = 0; c < pz.W; ++c) {
            p.offs.push_back({0, c});
            p.cells.push_back(r * pz.W + c);
        }
        pend.push_back(std::move(p));
    }
    return Finalise(pz, pend, "rows");
}

Dissection MakeColumnStripDissection(const Puzzle& pz) {
    std::vector<Pending> pend;
    for (int c = 0; c < pz.W; ++c) {
        Pending p;
        for (int r = 0; r < pz.H; ++r) {
            p.offs.push_back({r, 0});
            p.cells.push_back(r * pz.W + c);
        }
        pend.push_back(std::move(p));
    }
    return Finalise(pz, pend, "columns");
}

Dissection MakeMixedDissection(const Puzzle& pz, uint64_t seed, double pL, double pZ, double pD) {
    // L-trominoes (2x2 box, four orientations), Z and S tetrominoes (2x3 box),
    // dominoes, and monominoes for the leftovers.
    const std::vector<Offs> shapes = {
        {{0, 0}, {1, 0}, {1, 1}},              // L
        {{0, 0}, {0, 1}, {1, 0}},              // L
        {{0, 0}, {0, 1}, {1, 1}},              // L
        {{0, 0}, {1, -1}, {1, 0}},             // L (2x2 minus the top-left)
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}},      // Z
        {{0, 0}, {0, 1}, {1, -1}, {1, 0}},     // S
        {{0, 0}, {0, 1}},                      // domino H
        {{0, 0}, {1, 0}},                      // domino V
    };
    std::vector<char> used(pz.ncells, 0);
    std::vector<Pending> pend;
    std::mt19937_64 rng(seed);
    auto uni = [&]() { return (double)(rng() % 1000000) / 1000000.0; };
    for (int cell = 0; cell < pz.ncells; ++cell) {
        if (used[cell]) continue;
        const int r0 = cell / pz.W, c0 = cell % pz.W;
        // try a random shape that fits with `cell` as its top-left-most cell
        bool placed = false;
        for (int attempt = 0; attempt < 24 && !placed; ++attempt) {
            const double u = uni();
            int lo = 0, hi = 8;
            if (u < pL) { lo = 0; hi = 4; }
            else if (u < pL + pZ) { lo = 4; hi = 6; }
            else if (u < pL + pZ + pD) { lo = 6; hi = 8; }
            else break;
            const Offs& sh = shapes[lo + (int)(rng() % (uint64_t)(hi - lo))];
            std::vector<int> cs;
            bool fits = true;
            for (auto& o : sh) {
                const int rr = r0 + o.first, cc = c0 + o.second;
                // BOTH edges matter: some shapes carry a negative column offset
                // (the L that is a 2x2 box minus its top-left cell, and the S
                // tetromino), and without the `cc < 0` test those wrap off the
                // left side of the board and write used[-1].
                if (rr < 0 || cc < 0 || rr >= pz.H || cc >= pz.W ||
                    used[rr * pz.W + cc]) {
                    fits = false;
                    break;
                }
                cs.push_back(rr * pz.W + cc);
            }
            if (!fits) continue;
            // INVARIANT: the anchor cell must belong to the shape.  Without
            // this, a shape such as the S-tetromino can be placed "in front
            // of" `cell`, leaving `cell` uncovered with the scan already past
            // it -- which silently produces a non-partition.
            if (std::find(cs.begin(), cs.end(), cell) == cs.end()) fits = false;
            if (!fits) continue;
            for (int c : cs) used[c] = 1;
            Pending p;
            p.offs = sh;
            p.cells = cs;
            pend.push_back(std::move(p));
            placed = true;
        }
        if (!placed) {
            used[cell] = 1;
            Pending p;
            p.offs = {{0, 0}};
            p.cells = {cell};
            pend.push_back(std::move(p));
        }
    }
    return Finalise(pz, pend, "mixed L/Z/domino");
}

// --------------------------------------------------------------------- the move
namespace {

// Cost (violated slot weight) of slot `sj` holding the content of group `gi`
// rotated by q.
int SlotCost(const Board& b, const Dissection& d, int gi, int sj, int q) {
    const Group& src = d.groups[gi];
    const Group& slot = d.groups[sj];
    int cost = 0;
    for (int code : d.bslots[sj]) {
        const int idx = code >> 2, s = code & 3;
        const int cell = slot.cells[idx];
        const int base = cell * 4 + s;
        const int nb = b.pz->neigh[base];
        const int w = (nb < 0) ? b.wBorderSlot : 2;
        const int i = src.inv[q][idx];
        const int srcCell = src.cells[i];
        const int piece = b.pc[srcCell];
        const int rot = (b.rt[srcCell] + src.inc[q]) & 3;
        const int nc = (int)b.protCol[piece * 16 + rot * 4 + s];
        if (nc != (int)b.target[base]) cost += w;
    }
    return cost;
}

int CurrentBoundaryCost(const Board& b, const Dissection& d, int gj) {
    const Group& g = d.groups[gj];
    int cost = 0;
    for (int code : d.bslots[gj]) {
        const int idx = code >> 2, s = code & 3;
        const int base = g.cells[idx] * 4 + s;
        const int nb = b.pz->neigh[base];
        if (b.col[base] != b.target[base]) cost += (nb < 0) ? b.wBorderSlot : 2;
    }
    return cost;
}

}  // namespace

ReassignResult DissectionReassign(Board& b, const Dissection& d, const ReassignParams& pr,
                                  uint64_t seed) {
    ReassignResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const int ng = (int)d.groups.size();
    if (ng < 2) return res;
    std::mt19937 rng((unsigned)seed);

    // ---- a maximal independent set of groups, in random order
    std::vector<int> order(ng);
    for (int i = 0; i < ng; ++i) order[i] = i;
    if (pr.shuffle) std::shuffle(order.begin(), order.end(), rng);
    std::vector<char> block(ng, 0);
    std::vector<int> S;
    for (int g : order) {
        if (block[g]) continue;
        if ((int)S.size() >= pr.maxGroups) break;
        S.push_back(g);
        block[g] = 1;
        for (int h : d.adj[g]) block[h] = 1;
    }
    const int m = (int)S.size();
    res.groups = m;
    if (m < pr.minGroups) return res;
    res.ran = true;

    // ---- cost matrices, ONE PER SHAPE CLASS.
    // A rotation never changes a shape, so a group can only ever occupy a slot
    // of its own shape.  In a homogeneous dissection (all dominoes, all
    // squares, all rows) that is automatic and the problem is one assignment;
    // in a mixed L/Z/domino dissection it is not, and folding every class into
    // a single matrix fills it with infeasible entries whose "optimum" is a
    // sentinel rather than a cost.
    std::map<int, std::vector<int>> classes;
    for (int g : S) classes[d.groups[g].shapeId].push_back(g);
    std::vector<int> posOf(ng, -1);
    for (int i = 0; i < m; ++i) posOf[S[i]] = i;

    const int kBad = 1 << 28;
    std::vector<int> srcOf(m, -1), qOf(m, 0);
    long long opt = 0;
    int assignments = 0;
    for (auto& kv : classes) {
        const std::vector<int>& C = kv.second;
        const int k = (int)C.size();
        std::vector<std::vector<int>> a((size_t)k, std::vector<int>(k, kBad));
        std::vector<std::vector<int>> bestQ((size_t)k, std::vector<int>(k, -1));
        for (int i = 0; i < k; ++i) {
            for (int j = 0; j < k; ++j) {
                const Group& src = d.groups[C[i]];
                int best = kBad, bq = -1;
                for (int q = 0; q < 4; ++q) {
                    if (!src.rotValid[q]) continue;
                    if (src.rotShape[q] != d.groups[C[j]].shapeId) continue;
                    const int c = SlotCost(b, d, C[i], C[j], q);
                    if (c < best) { best = c; bq = q; }
                }
                a[i][j] = best;
                bestQ[i][j] = bq;
            }
        }
        std::vector<int> colForRow;
        opt += HungarianMin(a, &colForRow);
        ++assignments;
        for (int i = 0; i < k; ++i) {
            const int j = colForRow[i];
            srcOf[posOf[C[j]]] = posOf[C[i]];
            qOf[posOf[C[j]]] = bestQ[i][j];
        }
    }
    for (int j = 0; j < m; ++j)
        if (srcOf[j] < 0) { res.seconds = 0; return res; }   // should not happen

    long long cur = 0;
    for (int j = 0; j < m; ++j) cur += CurrentBoundaryCost(b, d, S[j]);
    res.assignments = assignments;
    res.predicted = cur - opt;
    if (res.predicted <= 0) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- apply: slot j receives the content of group srcOf[j], rotated by qOf[j]
    const long long costBefore = b.cost;
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    std::vector<int> np(b.pz->ncells, 0), nr(b.pz->ncells, 0);
    for (int j = 0; j < m; ++j) {
        const Group& slot = d.groups[S[j]];
        const Group& src = d.groups[S[srcOf[j]]];
        const int q = qOf[j];
        for (size_t idx = 0; idx < slot.cells.size(); ++idx) {
            const int i = src.inv[q][idx];
            const int sc = src.cells[i];
            np[slot.cells[idx]] = b.pc[sc];
            nr[slot.cells[idx]] = (b.rt[sc] + src.inc[q]) & 3;
        }
    }
    for (int j = 0; j < m; ++j)
        for (int c : d.groups[S[j]].cells) {
            b.pc[c] = np[c];
            b.rt[c] = nr[c];
        }
    b.rebuild();
    res.costBefore = costBefore;
    res.costAfter = b.cost;
    res.actualGain = costBefore - b.cost;
    if (pr.acceptOnlyImproving && b.cost >= costBefore) {
        b.load(savedPc, savedRt);
        res.costAfter = costBefore;
        res.actualGain = 0;
    } else {
        res.improved = res.actualGain > 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

ReassignRepeatResult DissectionReassignRepeat(Board& b, const Dissection& d,
                                              const ReassignParams& pr, uint64_t seed,
                                              int maxRounds) {
    ReassignRepeatResult out;
    const auto t0 = std::chrono::steady_clock::now();
    out.costBefore = b.cost;
    uint64_t s = seed;
    for (int r = 0; r < maxRounds; ++r) {
        ++out.rounds;
        ReassignResult rr = DissectionReassign(b, d, pr, s);
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        if (rr.actualGain > 0) {
            ++out.improvedRounds;
            out.totalGain += rr.actualGain;
        } else {
            break;  // this dissection is exhausted
        }
    }
    out.costAfter = b.cost;
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace e2
