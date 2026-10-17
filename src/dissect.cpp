// Eternity-Blossom -- dissection-based exact reassignment.  See dissect.hpp.
// SPDX-License-Identifier: MIT

#include "e2/dissect.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <set>
#include <random>

#include "e2/assign.hpp"
#include "e2/matching.hpp"


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
    d.internal.assign(ng, {});
    for (int i = 0; i < ng; ++i) {
        for (size_t ii = 0; ii < d.groups[i].cells.size(); ++ii) {
            const int cell = d.groups[i].cells[ii];
            for (int s = 0; s < 4; ++s) {
                const int nb = pz.neigh[cell * 4 + s];
                if (nb < 0) { d.bslots[i].push_back((int)(ii * 4 + s)); continue; }
                const int j = d.groupOf[nb];
                if (j == i) {
                    // Internal to the group.  Record it once, not twice: an
                    // edge between two cells of the group is seen from both
                    // ends, and sides come in opposite pairs (N<->S, E<->W),
                    // so keeping only s in {N,E} keeps exactly one copy.
                    if (s < 2) {
                        const auto& cs = d.groups[i].cells;
                        const int jj = (int)(std::find(cs.begin(), cs.end(), nb) - cs.begin());
                        d.internal[i].push_back({(int)ii, s, jj});
                    }
                    continue;
                }
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

std::vector<std::vector<std::pair<int, int>>> AllOrientations(
    const std::vector<std::pair<int, int>>& base, bool reflect) {
    std::vector<std::vector<std::pair<int, int>>> out;
    std::vector<std::vector<std::pair<int, int>>> starts = {base};
    if (reflect) {
        auto m = base;
        int mc = 0;
        for (auto& p : m) mc = std::min(mc, p.second);
        for (auto& p : m) p.second = mc - (p.second - mc);   // mirror in c
        std::sort(m.begin(), m.end());
        int mr = INT_MAX, mc2 = INT_MAX;
        for (auto& p : m) { mr = std::min(mr, p.first); mc2 = std::min(mc2, p.second); }
        for (auto& p : m) { p.first -= mr; p.second -= mc2; }
        std::sort(m.begin(), m.end());
        if (m != base) starts.push_back(m);
    }
    std::set<std::vector<std::pair<int, int>>> seen;
    for (const auto& s0 : starts) {
        for (int q = 0; q < 4; ++q) {
            std::vector<std::pair<int, int>> o;
            std::vector<int> dst;
            RotateShapeOffsets(s0, q, o, dst);
            std::sort(o.begin(), o.end());
            if (seen.insert(o).second) out.push_back(o);
        }
    }
    return out;
}

namespace {
Dissection PolyominoTilingOnce(const Puzzle& pz, uint64_t seed,
    const std::vector<std::vector<std::pair<int, int>>>& shapes);
}  // namespace

Dissection MakePolyominoDissection(
    const Puzzle& pz, uint64_t seed,
    const std::vector<std::vector<std::pair<int, int>>>& shapes) {
    // A row-major greedy scan strands a scattering of monominoes that a perfect
    // tiling would not need -- 256 = 3*85 + 1 trominoes and 256 = 4*64
    // tetrominoes.  Restart with different random shape orders and keep the
    // tiling that covers the most cells properly.
    const int n = (int)shapes[0].size();
    Dissection bestD;
    int bestMon = INT_MAX;
    for (int restart = 0; restart < 24; ++restart) {
        Dissection d = PolyominoTilingOnce(pz, seed * 131071ull + (uint64_t)restart + 1, shapes);
        int mon = 0;
        for (const Group& g : d.groups) if (g.cells.size() == 1) ++mon;
        if (mon < bestMon) { bestMon = mon; bestD = std::move(d); }
        if (bestMon <= (pz.ncells % n)) break;      // cannot do better
    }
    return bestD;
}
namespace {
Dissection PolyominoTilingOnce(const Puzzle& pz, uint64_t seed,
    const std::vector<std::vector<std::pair<int, int>>>& shapes) {
    std::vector<char> used(pz.ncells, 0);
    std::vector<Pending> pend;
    std::mt19937_64 rng(seed);
    for (int cell = 0; cell < pz.ncells; ++cell) {
        if (used[cell]) continue;
        const int r0 = cell / pz.W, c0 = cell % pz.W;
        bool placed = false;
        // Walk the shapes in a fresh random order and take the FIRST that fits,
        // rather than sampling with replacement.  Sampling wastes attempts on
        // shapes that cannot fit and leaves unnecessary monominoes behind --
        // with 48 attempts of random sampling a tromino tiling of 16x16 was
        // stranding 7 single cells instead of the 1 that 256 = 3*85 + 1 allows.
        std::vector<int> idx(shapes.size());
        for (size_t t = 0; t < idx.size(); ++t) idx[t] = (int)t;
        std::shuffle(idx.begin(), idx.end(), rng);
        for (int si : idx) {
            if (placed) break;
            const Offs& sh = shapes[si];
            std::vector<int> cs;
            bool fits = true;
            for (auto& o : sh) {
                const int rr = r0 + o.first, cc = c0 + o.second;
                if (rr < 0 || cc < 0 || rr >= pz.H || cc >= pz.W ||
                    used[rr * pz.W + cc]) { fits = false; break; }
                cs.push_back(rr * pz.W + cc);
            }
            if (!fits) continue;
            // the anchor cell must belong to the shape, or the scan runs past
            // it and leaves it uncovered
            if (std::find(cs.begin(), cs.end(), cell) == cs.end()) continue;
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
    return Finalise(pz, pend, std::to_string(shapes[0].size()) + "-ominoes");
}
}  // namespace

Dissection MakeTrominoDissection(const Puzzle& pz, uint64_t seed) {
    // the two trominoes: the straight one and the L
    std::vector<std::vector<std::pair<int, int>>> sh;
    for (auto& o : AllOrientations({{0, 0}, {0, 1}, {0, 2}}, false)) sh.push_back(o);
    for (auto& o : AllOrientations({{0, 0}, {0, 1}, {1, 0}}, false)) sh.push_back(o);
    return MakePolyominoDissection(pz, seed, sh);
}

Dissection MakeTetrominoDissection(const Puzzle& pz, uint64_t seed) {
    std::vector<std::vector<std::pair<int, int>>> sh;
    for (auto& o : AllOrientations({{0, 0}, {0, 1}, {0, 2}, {0, 3}}, false)) sh.push_back(o); // I
    for (auto& o : AllOrientations({{0, 0}, {0, 1}, {1, 0}, {1, 1}}, false)) sh.push_back(o); // O
    for (auto& o : AllOrientations({{0, 0}, {0, 1}, {0, 2}, {1, 1}}, false)) sh.push_back(o); // T
    for (auto& o : AllOrientations({{0, 1}, {0, 2}, {1, 0}, {1, 1}}, true))  sh.push_back(o); // S/Z
    for (auto& o : AllOrientations({{0, 0}, {1, 0}, {2, 0}, {2, 1}}, true))  sh.push_back(o); // L/J
    return MakePolyominoDissection(pz, seed, sh);
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

// ------------------------------------------------- full rearrangement costing
// Under a RIGID rotation a group's internal edges are merely permuted, so their
// total cancels and SlotCost can ignore them.  Once pieces may permute and spin
// independently that is no longer true -- swapping the two halves of a domino
// changes the colour pair facing each other across its middle -- so the internal
// edges have to be costed, both in the baseline and in every candidate.
long long CurrentGroupCost(const Board& b, const Dissection& d, int g) {
    long long cost = CurrentBoundaryCost(b, d, g);
    const Group& grp = d.groups[g];
    for (const auto& e : d.internal[g]) {
        const int a = grp.cells[e[0]], c = grp.cells[e[2]];
        const int ca = (int)b.col[a * 4 + e[1]];
        const int cc = (int)b.col[c * 4 + (e[1] ^ 2)];
        if (ca != cc) cost += 2;
    }
    return cost;
}

long long SlotCostFull(const Board& b, const Dissection& d, int gi, int sj,
                       std::vector<int>* outPerm, std::vector<int>* outRot) {
    const Group& src = d.groups[gi];
    const Group& slot = d.groups[sj];
    const int n = (int)slot.cells.size();
    if ((int)src.cells.size() != n) return LLONG_MAX;

    // ---- precompute, for each slot cell k, what it costs to hold each of the
    //      group's pieces at each rotation.  These depend on nothing else, so
    //      the 4^n * n! enumeration below is then just table lookups.
    //      cellCost[k][i][r] -- slot cell k, source piece i, rotation r.
    std::vector<std::vector<std::array<long long, 4>>> cellCost(
        (size_t)n, std::vector<std::array<long long, 4>>(n));
    for (int k = 0; k < n; ++k) {
        const int cell = slot.cells[k];
        for (int i = 0; i < n; ++i) {
            const int piece = b.pc[src.cells[i]];
            const int baseRot = b.rt[src.cells[i]];
            for (int r = 0; r < 4; ++r) {
                const int rot = (baseRot + r) & 3;
                long long c = 0;
                for (int sd = 0; sd < 4; ++sd) {
                    const int bs = cell * 4 + sd;
                    const int nb = b.pz->neigh[bs];
                    if (nb >= 0 && d.groupOf[nb] == sj) continue;   // internal, costed below
                    const int w = (nb < 0) ? b.wBorderSlot : 2;
                    const int nc = (int)b.protCol[piece * 16 + rot * 4 + sd];
                    if (nc != (int)b.target[bs]) c += w;
                }
                cellCost[k][i][r] = c;
            }
        }
    }
    // colour presented by source piece i at rotation r on side sd
    std::vector<std::array<std::array<int, 4>, 4>> pcol((size_t)n);
    for (int i = 0; i < n; ++i) {
        const int piece = b.pc[src.cells[i]];
        const int baseRot = b.rt[src.cells[i]];
        for (int r = 0; r < 4; ++r)
            for (int sd = 0; sd < 4; ++sd)
                pcol[i][r][sd] = (int)b.protCol[piece * 16 + ((baseRot + r) & 3) * 4 + sd];
    }

    // ---- enumerate all 4^n * n! placements
    long long best = LLONG_MAX;
    std::vector<int> bestPerm(n), bestRot(n);
    std::vector<int> perm(n);
    for (int i = 0; i < n; ++i) perm[i] = i;
    std::vector<int> rot(n, 0);
    do {
        for (long long code = 0; code < (1LL << (2 * n)); ++code) {
            for (int k = 0; k < n; ++k) rot[k] = (int)((code >> (2 * k)) & 3);
            long long c = 0;
            for (int k = 0; k < n; ++k) {
                c += cellCost[k][perm[k]][rot[k]];
                if (c >= best) break;
            }
            if (c >= best) continue;
            for (const auto& e : d.internal[sj]) {
                const int ck = pcol[perm[e[0]]][rot[e[0]]][e[1]];
                const int cl = pcol[perm[e[2]]][rot[e[2]]][e[1] ^ 2];
                if (ck != cl) c += 2;
            }
            if (c < best) {
                best = c;
                bestPerm = perm;
                bestRot = rot;
            }
        }
    } while (std::next_permutation(perm.begin(), perm.end()));

    if (outPerm) *outPerm = bestPerm;
    if (outRot) *outRot = bestRot;
    return best;
}

// ------------------------------------------------------- the NON-BIPARTITE move
SwapMatchResult SwapMatchPass(Board& b, const Dissection& d, const SwapMatchParams& pr,
                              MatchingStats* ms) {
    SwapMatchResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const int ng = (int)d.groups.size();

    // ---- candidates: groups whose own cost is not already zero
    std::vector<int> cand;
    std::vector<long long> curCost(ng, 0);
    for (int i = 0; i < ng; ++i) {
        const long long c = CurrentGroupCost(b, d, i);
        curCost[i] = c;
        if (c > 0) cand.push_back(i);
    }
    if ((int)cand.size() > pr.maxCandidates) {
        std::stable_sort(cand.begin(), cand.end(),
                         [&](int x, int y) { return curCost[x] > curCost[y]; });
        cand.resize(pr.maxCandidates);
        std::sort(cand.begin(), cand.end());
    }
    const int k = (int)cand.size();
    res.candidates = k;
    if (k < 2) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    res.ran = true;

    // ---- the full placement cost of every ordered pair, plus the arrangement
    //      that achieves it.  cost[i][j] = best cost of putting i's pieces into
    //      j's cells, over all 4^n * n! placements.
    std::vector<std::vector<long long>> cost((size_t)k, std::vector<long long>(k, 0));
    std::vector<std::vector<std::vector<int>>> perm(
        (size_t)k, std::vector<std::vector<int>>(k));
    std::vector<std::vector<std::vector<int>>> rot(
        (size_t)k, std::vector<std::vector<int>>(k));
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            if (i == j) { cost[i][j] = curCost[cand[i]]; continue; }
            if (d.adjacent(cand[i], cand[j])) continue;
            std::vector<int> pm, rt;
            cost[i][j] = SlotCostFull(b, d, cand[i], cand[j], &pm, &rt);
            perm[i][j] = pm;
            rot[i][j] = rt;
        }
    }

    // ---- the swap graph: complement of the group adjacency graph, weight = gain
    auto gainOf = [&](int i, int j) {
        if (d.adjacent(cand[i], cand[j])) return (long long)-1;
        return curCost[cand[i]] + curCost[cand[j]] - cost[j][i] - cost[i][j];
    };
    std::vector<std::tuple<int, int, int>> edges;
    for (int i = 0; i < k; ++i)
        for (int j = i + 1; j < k; ++j) {
            const long long g = gainOf(i, j);
            if (g > 0) edges.emplace_back(i, j, (int)g);
        }
    res.pairsOffered = (int)edges.size();
    if (edges.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- Blossom.  Iteratively: solve, take the best pair of the matching,
    //      delete it and its neighbours, re-solve on what is left.  A single
    //      solve followed by a greedy filter throws away pairs that touch; this
    //      keeps finding more.
    std::vector<std::pair<int, int>> applyList;
    std::vector<char> dead(k, 0);
    while (true) {
        std::vector<std::tuple<int, int, int>> live;
        for (auto& e : edges)
            if (!dead[std::get<0>(e)] && !dead[std::get<1>(e)]) live.push_back(e);
        if (live.empty()) break;

        std::vector<std::pair<int, int>> pairs;
        long long w = 0;
        ++res.blossomCalls;
        res.blossomVertices += k;
        res.blossomEdges += (long long)live.size();
        if (!MaxWeightMatching(k, live, pairs, w, ms)) { res.error = "matching failed"; break; }
        if (pairs.empty()) break;

        // take the heaviest pair in this matching
        int bi = -1, bj = -1, bw = -1;
        for (auto& p : pairs) {
            const long long g = gainOf(p.first, p.second);
            if ((int)g > bw) { bw = (int)g; bi = p.first; bj = p.second; }
        }
        if (bi < 0) break;
        applyList.push_back({bi, bj});
        res.pairsChosen++;
        if (!pr.iterative) {
            // keep the rest of this matching only if they do not touch anything
            for (auto& p : pairs) {
                if (p.first == bi && p.second == bj) continue;
                bool clash = false;
                for (auto& q : applyList)
                    if (d.adjacent(cand[q.first], cand[p.first]) ||
                        d.adjacent(cand[q.first], cand[p.second]) ||
                        d.adjacent(cand[q.second], cand[p.first]) ||
                        d.adjacent(cand[q.second], cand[p.second])) { clash = true; break; }
                if (!clash) applyList.push_back(p);
            }
            break;
        }
        // delete the pair and every group touching it
        dead[bi] = dead[bj] = 1;
        for (int t = 0; t < k; ++t)
            if (!dead[t] && (d.adjacent(cand[t], cand[bi]) || d.adjacent(cand[t], cand[bj])))
                dead[t] = 1;
    }
    if ((int)applyList.size() > pr.maxPairs) applyList.resize(pr.maxPairs);
    if (applyList.empty()) {
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // ---- apply
    const long long before = b.cost;
    const std::vector<int> savedPc = b.pc, savedRt = b.rt;
    std::vector<int> np = b.pc, nr = b.rt;
    long long predicted = 0;
    for (auto& p : applyList) {
        const int i = p.first, j = p.second;
        predicted += gainOf(i, j);
        const Group& gi = d.groups[cand[i]];
        const Group& gj = d.groups[cand[j]];
        // i's cells receive j's pieces, arranged per SlotCostFull(j -> i)
        for (size_t t = 0; t < gi.cells.size(); ++t) {
            const int from = gj.cells[perm[j][i][t]];
            np[gi.cells[t]] = b.pc[from];
            nr[gi.cells[t]] = (b.rt[from] + rot[j][i][t]) & 3;
        }
        // j's cells receive i's pieces, arranged per SlotCostFull(i -> j)
        for (size_t t = 0; t < gj.cells.size(); ++t) {
            const int from = gi.cells[perm[i][j][t]];
            np[gj.cells[t]] = b.pc[from];
            nr[gj.cells[t]] = (b.rt[from] + rot[i][j][t]) & 3;
        }
    }
    // only write the cells that actually move
    for (auto& p : applyList) {
        for (int c : d.groups[cand[p.first]].cells) { b.pc[c] = np[c]; b.rt[c] = nr[c]; }
        for (int c : d.groups[cand[p.second]].cells) { b.pc[c] = np[c]; b.rt[c] = nr[c]; }
    }
    b.rebuild();
    res.pairsApplied = (int)applyList.size();
    res.predicted = predicted;
    res.actualGain = before - b.cost;
    if (pr.acceptOnlyImproving && b.cost >= before) {
        b.load(savedPc, savedRt);
        res.actualGain = 0;
    } else {
        res.improved = res.actualGain > 0;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

ReassignResult DissectionReassign(Board& b, const Dissection& d, const ReassignParams& pr,
                                  uint64_t seed) {
    ReassignResult res;
    const auto t0 = std::chrono::steady_clock::now();
    const int ng = (int)d.groups.size();
    if (ng < 2) return res;
    std::mt19937 rng((unsigned)seed);

    // ---- an independent set of groups, as large as we can cheaply get.
    // A bigger set is a bigger assignment problem, so it is worth spending a
    // little on: randomised greedy restarts, then an improvement pass that
    // tries dropping one member and greedily refilling the hole (a 1-for-2
    // swap) whenever that grows the set.
    std::vector<int> order(ng);
    for (int i = 0; i < ng; ++i) order[i] = i;

    auto greedyFrom = [&](const std::vector<int>& ord) {
        std::vector<char> blk(ng, 0);
        std::vector<int> out;
        for (int g : ord) {
            if (blk[g]) continue;
            if ((int)out.size() >= pr.maxGroups) break;
            out.push_back(g);
            blk[g] = 1;
            for (int h : d.adj[g]) blk[h] = 1;
        }
        return out;
    };
    // minimum-degree-first is a much better order than random for MIS
    auto minDegreeOrder = [&]() {
        std::vector<int> deg(ng);
        for (int i = 0; i < ng; ++i) deg[i] = (int)d.adj[i].size();
        std::vector<char> used(ng, 0);
        std::vector<int> out;
        out.reserve(ng);
        for (int it = 0; it < ng; ++it) {
            int bestv = -1, bd = 1 << 30;
            for (int i = 0; i < ng; ++i)
                if (!used[i] && deg[i] < bd) { bd = deg[i]; bestv = i; }
            used[bestv] = 1;
            out.push_back(bestv);
            for (int h : d.adj[bestv]) deg[h]--;
        }
        return out;
    };

    std::vector<int> S;
    const int trials = std::max(1, pr.misTrials);
    for (int t = 0; t < trials; ++t) {
        std::vector<int> ord = order;
        if (t == 0 || !pr.shuffle) ord = minDegreeOrder();
        else std::shuffle(ord.begin(), ord.end(), rng);
        std::vector<int> cand = greedyFrom(ord);
        if ((int)cand.size() > (int)S.size()) S = cand;
    }
    if (pr.misImprove && !S.empty()) {
        bool grew = true;
        while (grew) {
            grew = false;
            for (size_t at = 0; at < S.size() && !grew; ++at) {
                std::vector<char> inS(ng, 0);
                for (int g : S) inS[g] = 1;
                inS[S[at]] = 0;
                std::vector<char> blk(ng, 0);
                std::vector<int> out;
                std::vector<int> ord = minDegreeOrder();
                for (int g : ord) {
                    if (blk[g]) continue;
                    if (!inS[g] && (int)out.size() >= pr.maxGroups) continue;
                    bool ok = true;
                    for (int h : d.adj[g]) if (inS[h] || blk[h]) { ok = false; break; }
                    if (!ok) continue;
                    out.push_back(g);
                    blk[g] = 1;
                    for (int h : d.adj[g]) blk[h] = 1;
                }
                if ((int)out.size() > (int)S.size()) { S = out; grew = true; }
            }
        }
    }
    std::sort(S.begin(), S.end());
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
    // Under fullRearrange the pieces are loose, so a group is compatible with a
    // slot of the same SIZE -- the shape no longer has to match.  Without it a
    // rigid rotation preserves shape, so shape is the compatibility key.
    std::map<int, std::vector<int>> classes;
    for (int g : S)
        classes[pr.fullRearrange ? (int)d.groups[g].cells.size() : d.groups[g].shapeId]
            .push_back(g);
    std::vector<int> posOf(ng, -1);
    for (int i = 0; i < m; ++i) posOf[S[i]] = i;

    const int kBad = 1 << 28;
    std::vector<int> srcOf(m, -1), qOf(m, 0);
    std::vector<char> useFull(m, 0);
    std::vector<std::vector<int>> permOf(m), rotOf(m);
    long long opt = 0;
    int assignments = 0;
    for (auto& kv : classes) {
        const std::vector<int>& C = kv.second;
        const int k = (int)C.size();
        std::vector<std::vector<int>> a((size_t)k, std::vector<int>(k, kBad));
        std::vector<std::vector<int>> bestQ((size_t)k, std::vector<int>(k, -1));
        // placement[k] for the chosen (i,j): perm and rot, k = 0..n-1
        std::vector<std::vector<std::vector<int>>> perms(
            (size_t)k, std::vector<std::vector<int>>(k));
        std::vector<std::vector<std::vector<int>>> rots(
            (size_t)k, std::vector<std::vector<int>>(k));
        const bool full = pr.fullRearrange &&
                          (int)d.groups[C[0]].cells.size() <= pr.maxCells;
        for (int i = 0; i < k; ++i) {
            for (int j = 0; j < k; ++j) {
                int best = kBad;
                if (full) {
                    std::vector<int> pm, rt;
                    const long long c = SlotCostFull(b, d, C[i], C[j], &pm, &rt);
                    if (c < (long long)kBad) {
                        best = (int)c;
                        perms[i][j] = pm;
                        rots[i][j] = rt;
                    }
                } else {
                    const Group& src = d.groups[C[i]];
                    int bq = -1;
                    for (int q = 0; q < 4; ++q) {
                        if (!src.rotValid[q]) continue;
                        if (src.rotShape[q] != d.groups[C[j]].shapeId) continue;
                        const int c = SlotCost(b, d, C[i], C[j], q);
                        if (c < best) { best = c; bq = q; }
                    }
                    bestQ[i][j] = bq;
                }
                a[i][j] = best;
            }
        }
        std::vector<int> colForRow;
        opt += HungarianMin(a, &colForRow);
        ++assignments;
        for (int i = 0; i < k; ++i) {
            const int j = colForRow[i];
            srcOf[posOf[C[j]]] = posOf[C[i]];
            qOf[posOf[C[j]]] = bestQ[i][j];
            if (full) {
                permOf[posOf[C[j]]] = perms[i][j];
                rotOf[posOf[C[j]]] = rots[i][j];
                useFull[posOf[C[j]]] = 1;
            }
        }
    }
    for (int j = 0; j < m; ++j)
        if (srcOf[j] < 0) { res.seconds = 0; return res; }   // should not happen

    // The baseline has to be measured the same way as the candidates.  With
    // fullRearrange the internal edges are in play, so they belong in `cur` too.
    long long cur = 0;
    for (int j = 0; j < m; ++j)
        cur += useFull[j] ? CurrentGroupCost(b, d, S[j]) : CurrentBoundaryCost(b, d, S[j]);
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
        if (useFull[j]) {
            // pieces placed one by one: slot cell k gets source piece perm[k]
            // spun by rot[k] on top of the rotation it already had
            for (size_t idx = 0; idx < slot.cells.size(); ++idx) {
                const int sc = src.cells[permOf[j][idx]];
                np[slot.cells[idx]] = b.pc[sc];
                nr[slot.cells[idx]] = (b.rt[sc] + rotOf[j][idx]) & 3;
            }
        } else {
            const int q = qOf[j];
            for (size_t idx = 0; idx < slot.cells.size(); ++idx) {
                const int i = src.inv[q][idx];
                const int sc = src.cells[i];
                np[slot.cells[idx]] = b.pc[sc];
                nr[slot.cells[idx]] = (b.rt[sc] + src.inc[q]) & 3;
            }
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
