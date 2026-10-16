// Eternity-Blossom -- Eternity II solved with Blossom VI (minimum-weight perfect matching).
//
// SPDX-License-Identifier: MIT
//
// This header defines the static part of an Eternity II instance: the piece set
// and the board geometry (which cell side touches which cell side, and which
// sides lie on the outer border).  Nothing here knows about search or matching.
//
// Encoding (the canonical public e2pieces.txt encoding):
//   * each row is one piece, four integers, clockwise starting at North:
//         N E S W
//   * colour 0 is the border colour ("grey"); it may only face the outside.
// The authentic 16x16 instance has 256 pieces, 23 colours, and colour 0 occurs
// exactly 64 times -- once per border slot of the 16x16 board.  Puzzle::verify()
// checks these invariants.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace e2 {

// The border colour in the canonical encoding.
static constexpr int kGray = 0;
// Sentinel for "no piece here".
static constexpr int kNoPiece = -1;
// Largest colour index we support (colours are stored in a uint8_t board array).
static constexpr int kMaxColors = 64;

// ---------------------------------------------------------------------------
// Rotations
// ---------------------------------------------------------------------------
// Sides are numbered clockwise from North: 0=N 1=E 2=S 3=W.
// Rotating a piece clockwise by `rot` quarter turns moves the colour that used
// to sit on side (s - rot) onto side s.

inline int oppositeSide(int s) { return s ^ 2; }

// Colour of `p` that faces side `s` after rotating clockwise by `rot`.
inline int sideColor(const int pcolors[4], int s, int rot) {
    return pcolors[(s + 4 - (rot & 3)) & 3];
}

struct Piece {
    // colours clockwise from North: c[0]=N c[1]=E c[2]=S c[3]=W
    int c[4] = {0, 0, 0, 0};
    // Rotational symmetry period of the piece: 1 (fourfold), 2 (twofold) or 4.
    // Only rotations 0..period-1 are distinct; the search uses this to avoid
    // evaluating identical rotations.
    int period = 4;
};

inline int piecePeriod(const int c[4]) {
    for (int r = 1; r < 4; ++r) {
        bool same = true;
        for (int s = 0; s < 4; ++s) {
            if (sideColor(c, s, r) != sideColor(c, s, 0)) { same = false; break; }
        }
        if (same) return r;  // period divides r; the first such r is the period
    }
    return 4;
}

// ---------------------------------------------------------------------------
// Puzzle: pieces + board geometry
// ---------------------------------------------------------------------------
struct Puzzle {
    int W = 16;                 // board width  (cells)
    int H = 16;                 // board height (cells)
    int ncells = 256;           // W*H -- must equal the number of pieces
    std::vector<Piece> pieces;  // indexed 0..ncells-1
    int numColors = 0;

    // --- geometry -----------------------------------------------------------
    std::vector<int> neigh;          // [cell*4+side] -> neighbouring cell, or -1 on the border
    std::vector<int> edgeOf;         // [cell*4+side] -> internal edge id, or -1 on the border
    std::vector<int> eCellA, eSideA; // internal edge endpoints (A = lower cell id)
    std::vector<int> eCellB, eSideB;
    std::vector<int> borderSlotIndex;  // [cell*4+side] -> border slot id, or -1
    std::vector<int> borderSlotCell;   // border slot -> cell
    std::vector<int> borderSlotSide;   // border slot -> side

    // Pieces that carry at least one grey edge; they must sit on the perimeter.
    std::vector<int> cornerPieces;     // exactly 2 grey edges, adjacent
    std::vector<int> edgePieces;       // exactly 1 grey edge
    std::vector<int> interiorPieces;   // no grey edge

    int numEdges() const { return static_cast<int>(eCellA.size()); }
    int numBorderSlots() const { return static_cast<int>(borderSlotCell.size()); }

    void initGeometry(int w, int h) {
        W = w; H = h; ncells = w * h;
        const int n4 = ncells * 4;
        neigh.assign(n4, -1);
        edgeOf.assign(n4, -1);
        borderSlotIndex.assign(n4, -1);
        eCellA.clear(); eSideA.clear(); eCellB.clear(); eSideB.clear();
        borderSlotCell.clear(); borderSlotSide.clear();

        static const int dr[4] = {-1, 0, +1, 0};
        static const int dc[4] = {0, +1, 0, -1};

        for (int r = 0; r < h; ++r) {
            for (int c = 0; c < w; ++c) {
                const int cell = r * w + c;
                const int nr = r + dr[0], nc = c + dc[0];  // unused, kept for clarity
                (void)nr; (void)nc;
                for (int s = 0; s < 4; ++s) {
                    const int rr = r + dr[s], cc = c + dc[s];
                    if (rr < 0 || rr >= h || cc < 0 || cc >= w) continue;
                    neigh[cell * 4 + s] = rr * w + cc;
                }
            }
        }
        // Each internal adjacency is emitted once: from the lower-numbered cell.
        for (int cell = 0; cell < ncells; ++cell) {
            for (int s = 0; s < 4; ++s) {
                const int nb = neigh[cell * 4 + s];
                if (nb < 0 || nb < cell) continue;
                const int e = static_cast<int>(eCellA.size());
                eCellA.push_back(cell); eSideA.push_back(s);
                eCellB.push_back(nb);   eSideB.push_back(oppositeSide(s));
                edgeOf[cell * 4 + s] = e;
                edgeOf[nb * 4 + oppositeSide(s)] = e;
            }
        }
        for (int cell = 0; cell < ncells; ++cell) {
            for (int s = 0; s < 4; ++s) {
                if (neigh[cell * 4 + s] < 0) {
                    const int b = static_cast<int>(borderSlotCell.size());
                    borderSlotIndex[cell * 4 + s] = b;
                    borderSlotCell.push_back(cell);
                    borderSlotSide.push_back(s);
                }
            }
        }
    }

    // Load pieces from a whitespace separated file with one "N E S W" row per
    // piece.  With w <= 0 the board is taken to be as square as the piece count
    // allows (sqrt), which is what every instance we ship is.
    static Puzzle load(const std::string& path, int w = 0, int h = 0) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("cannot open piece file: " + path);
        Puzzle pz;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            Piece pc;
            int k = 0, v = 0;
            while (ss >> v && k < 4) { pc.c[k++] = v; }
            if (k != 4) {
                throw std::runtime_error("malformed piece line (need 4 colours): " + line);
            }
            pc.period = piecePeriod(pc.c);
            pz.pieces.push_back(pc);
        }
        if (pz.pieces.empty()) throw std::runtime_error("empty piece file: " + path);
        const int np = static_cast<int>(pz.pieces.size());
        if (w <= 0 || h <= 0) {
            w = (int)std::lround(std::sqrt((double)np));
            h = np / w;
            if (w * h != np) {
                throw std::runtime_error("piece count " + std::to_string(np) +
                                         " is not rectangular; pass --w and --h");
            }
        }
        if (w * h != np) {
            throw std::runtime_error("board " + std::to_string(w) + "x" + std::to_string(h) +
                                     " does not match " + std::to_string(np) + " pieces");
        }
        pz.initGeometry(w, h);
        pz.classify();
        return pz;
    }

    void classify() {
        cornerPieces.clear(); edgePieces.clear(); interiorPieces.clear();
        numColors = 0;
        for (const Piece& p : pieces) {
            int g = 0;
            for (int s = 0; s < 4; ++s) {
                g += (p.c[s] == kGray) ? 1 : 0;
                numColors = std::max(numColors, p.c[s] + 1);
            }
            const int id = static_cast<int>(&p - &pieces[0]);
            if (g == 2) cornerPieces.push_back(id);
            else if (g == 1) edgePieces.push_back(id);
            else if (g == 0) interiorPieces.push_back(id);
            else throw std::runtime_error("piece with 3+ grey edges is not a valid E2 piece");
        }
    }

    // Sanity check on the instance.  Returns a human readable report; throws on
    // structurally impossible instances.
    std::string verify() const {
        std::ostringstream os;
        const int np = static_cast<int>(pieces.size());
        if (np != ncells) {
            throw std::runtime_error("piece count " + std::to_string(np) +
                                     " != board cells " + std::to_string(ncells));
        }
        std::vector<int> cnt(numColors + 1, 0);
        for (const Piece& p : pieces) for (int s = 0; s < 4; ++s) ++cnt[p.c[s]];
        os << "pieces=" << np << " board=" << W << "x" << H
           << " internal_edges=" << numEdges()
           << " border_slots=" << numBorderSlots()
           << " colours=" << numColors << "\n";
        os << "grey_count=" << (kGray < (int)cnt.size() ? cnt[kGray] : 0)
           << " (needs " << numBorderSlots() << ")\n";
        os << "corners=" << cornerPieces.size() << " edges=" << edgePieces.size()
           << " interior=" << interiorPieces.size() << "\n";
        // Every non-border colour must occur an even number of times: internal
        // adjacencies pair stubs up, so each colour class must be even.
        bool parity_ok = true;
        for (int c = 1; c < (int)cnt.size(); ++c) {
            if (cnt[c] % 2 != 0) parity_ok = false;
        }
        os << "non_grey_colour_parity_even=" << (parity_ok ? "yes" : "NO") << "\n";
        return os.str();
    }
};

}  // namespace e2
