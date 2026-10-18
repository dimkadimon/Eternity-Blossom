// Eternity-Blossom -- search implementation.
//
// SPDX-License-Identifier: MIT

#include "e2/coarse.hpp"
#include "e2/rotation.hpp"
#include "e2/search.hpp"
#include "e2/subsolve.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>

namespace e2 {

namespace {

std::vector<int> BuildCellOrder(const Puzzle& pz) {
    // Perimeter first (corners, then the rest of the border), then the interior
    // row by row.  Greedy construction benefits from doing the most constrained
    // cells first, because at that point there is still a large pool to choose
    // from.
    std::vector<int> order;
    order.push_back(0);
    order.push_back(pz.W - 1);
    order.push_back((pz.H - 1) * pz.W);
    order.push_back(pz.H * pz.W - 1);
    for (int c = 1; c < pz.W - 1; ++c) order.push_back(c);
    for (int r = 1; r < pz.H - 1; ++r) order.push_back(r * pz.W + pz.W - 1);
    for (int c = 1; c < pz.W - 1; ++c) order.push_back((pz.H - 1) * pz.W + c);
    for (int r = 1; r < pz.H - 1; ++r) order.push_back(r * pz.W);
    std::vector<char> seen(pz.ncells, 0);
    for (int c : order) seen[c] = 1;
    for (int c = 0; c < pz.ncells; ++c) if (!seen[c]) order.push_back(c);
    return order;
}

}  // namespace

// ---------------------------------------------------------------------------
BoardCheck CheckBoard(const Puzzle& pz, const std::vector<int>& pc, const std::vector<int>& rt) {
    BoardCheck ck;
    const int n = pz.ncells;
    if ((int)pc.size() != n || (int)rt.size() != n) return ck;
    std::vector<int> useCount(n, 0);
    for (int cell = 0; cell < n; ++cell) {
        const int p = pc[cell];
        if (p < 0 || p >= n) return ck;
        ++useCount[p];
    }
    for (int i = 0; i < n; ++i) if (useCount[i] != 1) ++ck.duplicatePieces;
    ck.valid = (ck.duplicatePieces == 0);
    for (int e = 0; e < pz.numEdges(); ++e) {
        const int ca = sideColor(pz.pieces[pc[pz.eCellA[e]]].c, pz.eSideA[e], rt[pz.eCellA[e]]);
        const int cb = sideColor(pz.pieces[pc[pz.eCellB[e]]].c, pz.eSideB[e], rt[pz.eCellB[e]]);
        if (ca == cb) ++ck.matched; else ++ck.mismatched;
    }
    for (int b = 0; b < pz.numBorderSlots(); ++b) {
        const int cell = pz.borderSlotCell[b], s = pz.borderSlotSide[b];
        const int c = sideColor(pz.pieces[pc[cell]].c, s, rt[cell]);
        if (c != kGray) ++ck.borderBad;
    }
    return ck;
}

// ---------------------------------------------------------------------------
SearchOutcome SearchOnce(const Puzzle& pz, const SearchParams& params,
                         const std::vector<int>* startPc, const std::vector<int>* startRt) {
    const auto t0 = std::chrono::steady_clock::now();
    std::mt19937_64 rng(params.seed * 1000003ULL + 12345ULL);
    SearchOutcome out;
    SearchStats& st = out.stats;

    Board b;
    b.setBorderWeight(params.wBorder);
    b.init(pz);

    if (startPc && startRt) {
        b.pc = *startPc; b.rt = *startRt; b.rebuild();
    } else {
        switch (params.init) {
            case InitMode::Random: b.randomize(rng); break;
            case InitMode::StructuredRandom: b.randomizeStructured(rng); break;
            case InitMode::Greedy: b.greedyConstruct(rng, 0.0, 1, nullptr); break;
            case InitMode::GreedyStructured: {
                const std::vector<int> order = BuildCellOrder(pz);
                b.greedyConstruct(rng, 0.05, 4, &order);
                break;
            }
        }
    }

    long long cost = b.cost;
    long long best = cost;
    std::vector<int> bestPc = b.pc, bestRt = b.rt;

    const int L = std::max(1, params.lahcLength);
    std::vector<long long> hist(L, cost);

    // --- Coarse dissections.  Square blocks are fixed; domino tilings are
    //     sampled once and then rotated through, so that each sweep explores a
    //     different set of 128 large moves on the same board state.
    std::vector<BlockSet> blockSets;
    for (int side : params.coarseSides) blockSets.push_back(MakeSquareBlocks(pz, side));
    for (int i = 0; i < params.coarseDominoes; ++i)
        blockSets.push_back(MakeDominoTiling(pz, params.seed * 7919u + 13u * (unsigned)i + 1u,
                                            params.coarseDominoFlips));
    long long coarseTick = 0;

    // --- Conflict frontier and colour index, rebuilt periodically.
    // `conflicts`  : cells that touch at least one violated slot.
    // `violEdges`  : the violated internal edges themselves.
    // `cellsByCol` : for each colour, cells whose piece carries it on some side.
    // Together they let the search propose a move that is *aimed* at a specific
    // violated adjacency instead of throwing two random cells together: pick a
    // violated edge, look at the colour it needs, and swap in a cell that
    // actually owns a piece carrying that colour.
    std::vector<int> conflicts;
    std::vector<int> violEdges;
    std::vector<std::vector<int>> cellsByCol(pz.numColors + 1);
    auto rebuildIndex = [&]() {
        conflicts.clear();
        for (int cell = 0; cell < pz.ncells; ++cell) if (b.cellCostA[cell] > 0) conflicts.push_back(cell);
        violEdges.clear();
        for (int e = 0; e < pz.numEdges(); ++e) {
            const int A = pz.eCellA[e], B = pz.eCellB[e];
            if (b.col[A * 4 + pz.eSideA[e]] != b.col[B * 4 + pz.eSideB[e]]) violEdges.push_back(e);
        }
        for (auto& v : cellsByCol) v.clear();
        for (int cell = 0; cell < pz.ncells; ++cell) {
            const int p = b.pc[cell];
            for (int s = 0; s < 4; ++s) {
                const int c = pz.pieces[p].c[s];
                if ((int)cellsByCol.size() <= c) cellsByCol.resize(c + 1);
                cellsByCol[c].push_back(cell);
            }
        }
    };
    rebuildIndex();
    auto pieceHasColor = [&](int piece, int c) {
        for (int s = 0; s < 4; ++s) if (pz.pieces[piece].c[s] == c) return true;
        return false;
    };

    long long sinceImprove = 0;
    long long iters = 0;
    // kick size actually used, and how many kicks in a row have failed
    double kickSize = (double)std::max(1, params.perturbSwaps);
    int kickFails = 0;
    long long lastBest = best;   // best as of the previous kick
    const auto deadline = t0 + std::chrono::duration<double>(params.timeLimitSeconds);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const bool useSA = (params.accept == AcceptMode::SA);
    auto temperature = [&]() {
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double f = el / std::max(1e-9, params.timeLimitSeconds);
        if (f > 1.0) f = 1.0;
        return params.saT0 * std::pow(params.saT1 / params.saT0, f);
    };
    double T = useSA ? temperature() : 0.0;

    auto timeLeft = [&]() {
        return std::chrono::steady_clock::now() < deadline;
    };
    double nextTrace = 1.0;
    auto maybeTrace = [&]() {
        if (!params.trace) return;
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (el < nextTrace) return;
        nextTrace = el + 2.0;
        std::printf("trace %.1fs  best=%lld  matched=%lld  iters=%lld\n", el, best,
                    (long long)pz.numEdges() - (best - params.wBorder * 0) / 2, iters);
        std::fflush(stdout);
    };

    while (timeLeft()) {
        // ---- one block of iterations between housekeeping checks
        for (int block = 0; block < 4096 && timeLeft(); ++block, ++iters) {
            if ((iters & 511) == 0) {
                rebuildIndex();
                if (useSA) T = temperature();
            }
            int a, bb;
            if (!violEdges.empty() && U(rng) < params.pTargeted && U(rng) < params.pCycle3) {
                // aimed 3-cycle: a violated cell, a colour-matched partner, and
                // a third cell chosen at random, all pairwise non-adjacent so
                // that the three cost terms stay independent
                const int e = violEdges[rng() % violEdges.size()];
                const int side = (rng() & 1) ? 0 : 1;
                a = side ? pz.eCellB[e] : pz.eCellA[e];
                const int want = b.target[a * 4 + (side ? pz.eSideB[e] : pz.eSideA[e])];
                bb = -1;
                if (want >= 0 && want < (int)cellsByCol.size() && !cellsByCol[want].empty()) {
                    for (int tries = 0; tries < 6; ++tries) {
                        const int cand = cellsByCol[want][rng() % cellsByCol[want].size()];
                        if (cand != a && pieceHasColor(b.pc[cand], want)) { bb = cand; break; }
                    }
                }
                if (bb < 0) { do { bb = (int)(rng() % pz.ncells); } while (bb == a); }
                int cc = -1;
                for (int tries = 0; tries < 4; ++tries) {
                    const int cand = (int)(rng() % pz.ncells);
                    if (cand == a || cand == bb) continue;
                    if (!b.pairwiseNonAdjacent3(a, bb, cand)) continue;
                    cc = cand; break;
                }
                if (cc >= 0) {
                    int rA = 0, rB = 0, rC = 0;
                    const int d3 = b.cycle3Delta(a, bb, cc, &rA, &rB, &rC);
                    const long long cand = cost + d3;
                    const int pos3 = (int)(iters % L);
                    const bool take3 = useSA
                        ? (cand <= cost || U(rng) < std::exp(-double(cand - cost) / T))
                        : (cand <= hist[pos3] || cand <= cost);
                    if (take3) {
                        b.applyCycle3(a, bb, cc, rA, rB, rC);
                        cost = cand;
                    }
                    hist[pos3] = cost;
                    if (cost < best) {
                        best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0;
                        if (best == 0) goto done;
                    } else {
                        ++sinceImprove;
                    }
                    continue;
                }
            }
            if (!violEdges.empty() && U(rng) < params.pTargeted) {
                // Aimed move: take a violated adjacency, and try to bring in a
                // piece that carries the colour this slot is asking for.
                const int e = violEdges[rng() % violEdges.size()];
                const int side = (rng() & 1) ? 0 : 1;
                a = side ? pz.eCellB[e] : pz.eCellA[e];
                const int target = b.target[a * 4 + (side ? pz.eSideB[e] : pz.eSideA[e])];
                bb = -1;
                if (target >= 0 && target < (int)cellsByCol.size() && !cellsByCol[target].empty()) {
                    for (int tries = 0; tries < 4; ++tries) {
                        const int cand = cellsByCol[target][rng() % cellsByCol[target].size()];
                        if (cand != a && pieceHasColor(b.pc[cand], target)) { bb = cand; break; }
                    }
                }
                if (bb < 0) {
                    do { bb = (int)(rng() % pz.ncells); } while (bb == a);
                }
            } else {
                do {
                    if (!conflicts.empty() && U(rng) < params.pConflictFirst) {
                        a = conflicts[rng() % conflicts.size()];
                        if (b.cellCostA[a] == 0) a = (int)(rng() % pz.ncells);
                    } else {
                        a = (int)(rng() % pz.ncells);
                    }
                    bb = (int)(rng() % pz.ncells);
                } while (bb == a);
            }

            long long cand;
            const int pos = (int)(iters % L);
            if (U(rng) < params.pRotate) {
                const int cell = a;
                const int r = b.bestRotation(cell);
                cand = cost + b.deltaRotate(cell, r);
                bool take = useSA ? (cand <= cost || U(rng) < std::exp(-double(cand - cost) / T))
                                  : (cand <= hist[pos] || cand <= cost);
                if (take) {
                    b.applyRotation(cell, r);
                    cost = cand;
                }
            } else {
                // "best of K proposals": evaluate up to sampleK candidate swaps
                // and move on the most promising one.  K = 1 is a plain random
                // proposal; larger K turns the search into steepest descent over
                // a sampled neighbourhood, which costs iterations but makes each
                // accepted move far better aimed.
                int ra = 0, rb = 0;
                int d = b.bestSwapRotation(a, bb, &ra, &rb);
                for (int k = 1; k < params.sampleK; ++k) {
                    int a2, b2;
                    if (!violEdges.empty() && U(rng) < params.pTargeted) {
                        const int e = violEdges[rng() % violEdges.size()];
                        const int side = (rng() & 1) ? 0 : 1;
                        a2 = side ? pz.eCellB[e] : pz.eCellA[e];
                        const int want = b.target[a2 * 4 + (side ? pz.eSideB[e] : pz.eSideA[e])];
                        b2 = -1;
                        if (want >= 0 && want < (int)cellsByCol.size() && !cellsByCol[want].empty()) {
                            for (int t = 0; t < 4; ++t) {
                                const int c2 = cellsByCol[want][rng() % cellsByCol[want].size()];
                                if (c2 != a2 && pieceHasColor(b.pc[c2], want)) { b2 = c2; break; }
                            }
                        }
                        if (b2 < 0) { do { b2 = (int)(rng() % pz.ncells); } while (b2 == a2); }
                    } else {
                        a2 = (int)(rng() % pz.ncells);
                        do { b2 = (int)(rng() % pz.ncells); } while (b2 == a2);
                    }
                    int ra2 = 0, rb2 = 0;
                    const int d2 = b.bestSwapRotation(a2, b2, &ra2, &rb2);
                    if (d2 < d) { d = d2; a = a2; bb = b2; ra = ra2; rb = rb2; }
                }
                cand = cost + d;
                bool take = useSA ? (cand <= cost || U(rng) < std::exp(-double(cand - cost) / T))
                                  : (cand <= hist[pos] || cand <= cost);
                if (take) {
                    b.applySwap(a, bb, ra, rb);
                    cost = cand;
                }
            }
            hist[pos] = cost;

            if (cost < best) {
                best = cost;
                bestPc = b.pc; bestRt = b.rt;
                sinceImprove = 0;
                if (best == 0) goto done;
            } else {
                ++sinceImprove;
            }
        }

        // ---- housekeeping
        maybeTrace();
        rebuildIndex();
        st.iterations = iters;

        if (best == 0) break;

        // Blossom packet of disjoint swaps: run it when the hill climber is
        // stuck, since the pass costs a few milliseconds and is only worth it
        // when local moves have stopped finding anything.
        if (params.useBlossomSwaps && params.blossomCheckEvery > 0 &&
            sinceImprove > params.blossomStallTrigger &&
            (iters % params.blossomCheckEvery) < 4096) {
            const long long before = b.cost;
            BlossomSwapResult r = BlossomSwapPass(b, params.blossom, &st.matching);
            ++st.blossomPasses;
            st.blossomTotalPredicted += r.predictedGain;
            st.blossomTotalActual += r.actualGain;
            if (r.actualGain > 0) ++st.blossomImproved; else ++st.blossomReverted;
            if (r.actualGain > 0) {
                cost = b.cost;
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                else sinceImprove = sinceImprove / 2;
                rebuildIndex();
                std::fill(hist.begin(), hist.end(), cost);
            }
            (void)before;
        }

        // Optimal reassignment: solve one matching problem that permutes the
        // pieces over the worst cells.  This is the operator that escapes the
        // plateaus where no single swap (or packet of swaps) helps.
        if (params.useAssign && params.assignCells >= 2 && params.assignEvery > 0 &&
            (iters % params.assignEvery) < 4096) {
            AssignParams ap = params.assign;
            ap.cells = params.assignCells;
            AssignResult ar = AssignReoptimizePass(b, ap, &st.matching);
            ++st.assignPasses;
            st.assignTotalActual += ar.actualGain;
            if (ar.actualGain > 0) {
                ++st.assignImproved;
                cost = b.cost;
                rebuildIndex();
                std::fill(hist.begin(), hist.end(), cost);
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                if (best == 0) break;
            }
        }

        // ---- Coarse rotate sweep: rigid block rotations, then (less often) the
        // Blossom packet of non-interacting block swaps on the coarse board.
        if (params.useCoarseRotate && params.coarseRotateEvery > 0 && !blockSets.empty() &&
            (iters % params.coarseRotateEvery) < 4096) {
            const BlockSet& bs = blockSets[(size_t)(coarseTick % (long long)blockSets.size())];
            ++coarseTick;
            const long long c0 = b.cost;
            CoarseRotateResult cr = CoarseRotateSweep(b, bs);
            ++st.coarseRotateSweeps;
            st.coarseRotates += cr.applied;
            if (b.cost != c0) {
                st.coarseRotateGain += c0 - b.cost;
                cost = b.cost;
                rebuildIndex();
                std::fill(hist.begin(), hist.end(), cost);
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                if (best == 0) break;
            }
        }

        if (params.useCoarseSwap && params.coarseSwapEvery > 0 && !blockSets.empty() &&
            (iters % params.coarseSwapEvery) < 4096) {
            const BlockSet& bs = blockSets[(size_t)(coarseTick % (long long)blockSets.size())];
            ++coarseTick;
            CoarseSwapResult cs = CoarseSwapPass(b, bs, params.coarseSwap, &st.matching);
            ++st.coarseSwapPasses;
            st.coarseSwapPredicted += cs.predictedGain;
            st.coarseSwapActual += cs.actualGain;
            st.coarseSwapPairs += cs.pairsApplied;
            if (cs.actualGain > 0) {
                ++st.coarseSwapImproved;
                cost = b.cost;
                rebuildIndex();
                std::fill(hist.begin(), hist.end(), cost);
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                if (best == 0) break;
            }
        }

        // ---- Exact rotation re-optimisation of whole strips (pathwidth DP).
        if (params.useRotStrip && params.rotStripEvery > 0 && (iters % params.rotStripEvery) < 4096) {
            const long long c0 = b.cost;
            StripSweepResult sr = StripRotateSweep(b, params.rotStripK, false);
            ++st.rotStripSweeps;
            if (params.rotStripColumns) {
                StripSweepResult sc = StripRotateSweep(b, params.rotStripK, true);
                sr.applied += sc.applied;
                sr.gain += sc.gain;
                sr.strips += sc.strips;
            }
            st.rotStripApplied += sr.applied;
            if (b.cost != c0) {
                st.rotStripGain += c0 - b.cost;
                cost = b.cost;
                rebuildIndex();
                std::fill(hist.begin(), hist.end(), cost);
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                if (best == 0) break;
            }
        }

        // ---- Exact sub-board branch and bound.
        if (params.useSubSolve && params.subSolveEvery > 0 && (iters % params.subSolveEvery) < 4096) {
            const int hh = std::max(1, std::min(params.subSolveH, pz.H));
            const int ww = std::max(1, std::min(params.subSolveW, pz.W));
            const int r0 = (int)(rng() % (unsigned)(pz.H - hh + 1));
            const int c0 = (int)(rng() % (unsigned)(pz.W - ww + 1));
            const std::vector<int> cells = RectRegion(pz, r0, c0, hh, ww);
            std::vector<int> pool(cells.size());
            for (size_t i = 0; i < cells.size(); ++i) pool[i] = b.pc[cells[i]];
            SubSolveResult ss = SubSolveRegion(b, cells, pool, params.subSolve, &st.matching);
            ++st.subSolveRuns;
            st.subSolveNodes += ss.nodes;
            st.subSolveStubCalls += ss.stubCalls;
            st.subSolveStubPrunes += ss.stubPrunes;
            if (ss.exact) ++st.subSolveExact;
            if (ss.improved) {
                ++st.subSolveImproved;
                cost = b.cost;
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                std::fill(hist.begin(), hist.end(), cost);
                rebuildIndex();
                if (best == 0) break;
            }
        }

        // ---- Rotation-only upper bound: an oracle, not a move.  If the best
        // board reachable from THIS placement by rotating alone is no better
        // than the best board we already have, the placement is finished with --
        // perturb it immediately instead of grinding on its rotations.
        if (params.useRotBound && params.rotBoundEvery > 0 && (iters % params.rotBoundEvery) < 4096) {
            RotationBoundResult rb = RotationUpperBound(b, params.rotBoundK, false);
            ++st.rotBoundCalls;
            if (rb.matchedUB < st.rotBoundBest) st.rotBoundBest = rb.matchedUB;
            // cost = 2*mismatches + wBorderSlot*badBorder >= 2*mismatchLB
            if (2 * rb.mismatchLB >= best) {
                ++st.rotBoundTriggers;
                for (int k = 0; k < params.perturbSwaps; ++k) {
                    const int u = (int)(rng() % pz.ncells);
                    int v = (int)(rng() % pz.ncells);
                    if (u == v) continue;
                    b.applySwap(u, v, b.rt[v], b.rt[u]);
                }
                cost = b.cost;
                std::fill(hist.begin(), hist.end(), cost);
                ++st.restarts;
                sinceImprove = 0;
                rebuildIndex();
            }
        }

        // Exact re-optimisation of a small region.
        if (params.useRegionLNS && params.regionEvery > 0 && (iters % params.regionEvery) < 4096) {
            std::vector<int> cells;
            // grow a connected blob
            std::vector<char> in(pz.ncells, 0);
            int seedCell = (int)(rng() % pz.ncells);
            cells.push_back(seedCell); in[seedCell] = 1;
            while ((int)cells.size() < params.regionSize) {
                const int src = cells[rng() % cells.size()];
                std::vector<int> nbs;
                for (int s = 0; s < 4; ++s) {
                    const int nb = pz.neigh[src * 4 + s];
                    if (nb >= 0 && !in[nb]) nbs.push_back(nb);
                }
                if (nbs.empty()) break;
                const int pick = nbs[rng() % nbs.size()];
                cells.push_back(pick); in[pick] = 1;
            }
            RegionResult rr = RegionReoptimize(b, cells, params.region, &st.matching);
            ++st.regionRuns;
            st.regionNodes += rr.nodes;
            st.regionBoundCalls += rr.boundCalls;
            st.regionBoundPrunes += rr.boundPrunes;
            if (rr.improved) {
                ++st.regionImproved;
                cost = b.cost;
                if (cost < best) { best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0; }
                std::fill(hist.begin(), hist.end(), cost);
                rebuildIndex();
            }
        }

        // Perturbation to escape a deep local optimum.
        if (sinceImprove > params.stallLimit) {
            // ---- learn the kick size from whether the LAST one worked
            if (params.adaptivePerturb) {
                if (best < lastBest) {
                    kickSize = std::max(1.0, kickSize * params.perturbShrink);
                    kickFails = 0;
                } else {
                    ++kickFails;
                    kickSize = std::min((double)params.perturbMax,
                                        kickSize * (1.0 + params.perturbGrowth));
                }
            } else {
                kickSize = (double)std::max(1, params.perturbSwaps);
            }
            lastBest = best;
            const int nKick = (int)(kickSize + U(rng));   // randomised rounding

            // ---- kick the INCUMBENT, not the drifted state.  Without this the
            // search discards its best board every time it stalls and restarts
            // from wherever LAHC/SA happened to have wandered to.
            if (params.perturbFromBest) {
                b.load(bestPc, bestRt);
                cost = b.cost;
            }
            for (int k = 0; k < nKick; ++k) {
                const int u = (int)(rng() % pz.ncells);
                int v = (int)(rng() % pz.ncells);
                if (u == v) continue;
                b.applySwap(u, v, b.rt[v], b.rt[u]);
            }
            // Chunk-level perturbation: random rigid block rotations (and
            // domino flips, if any dissections are loaded).  Each one keeps
            // every internal edge of its block intact and only re-exposes the
            // block's boundary, so it is a much larger step than a random swap
            // yet destroys far less structure.
            if (params.coarsePerturb > 0 && !blockSets.empty()) {
                for (int k = 0; k < params.coarsePerturb; ++k) {
                    const BlockSet& bs = blockSets[rng() % blockSets.size()];
                    const int bi = (int)(rng() % bs.blocks.size());
                    const int nrot = bs.blocks[bi].nrot;
                    if (nrot < 2) continue;
                    const int q = 1 + (int)(rng() % (unsigned)(nrot - 1));
                    ApplyBlockRotate(b, bs, bi, q);
                }
            }
            cost = b.cost;
            std::fill(hist.begin(), hist.end(), cost);
            ++st.restarts;
            sinceImprove = 0;
            rebuildIndex();

            // ---- repair the kicked board with the strong exact moves.  They
            // are worthless on a converged board and very strong on a damaged
            // one, so this is the one place in the loop where they pay.
            if (params.useNMoveRepair) {
                for (int round = 0; round < params.nmoveRounds; ++round) {
                    BlastParams bp = params.blast;
                    bp.maxCells = params.nmoveMaxCells;
                    bp.acceptOnlyImproving = true;
                    BlastResult br = BlastPass(b, bp, &st.matching);
                    if (br.actualGain > 0) { ++st.blastImproved; cost = b.cost; }

                    CycleCoverParams cp = params.cyccover;
                    cp.maxCells = params.nmoveMaxCells;
                    cp.acceptOnlyImproving = true;
                    CycleCoverResult cr = CycleCoverPass(b, cp, &st.matching);
                    if (cr.actualGain > 0) { ++st.cycImproved; cost = b.cost; }

                    if (br.actualGain <= 0 && cr.actualGain <= 0) break;
                    ++st.nmoveRounds;
                    rebuildIndex();
                }
                if (cost < best) {
                    best = cost; bestPc = b.pc; bestRt = b.rt; sinceImprove = 0;
                }
                std::fill(hist.begin(), hist.end(), cost);
            }
        }
    }
done:
    st.iterations = iters;
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out.pc = bestPc; out.rt = bestRt;
    out.cost = best;
    out.score = (long long)pz.numEdges() - best;  // only exact when borderBad == 0
    {
        BoardCheck ck = CheckBoard(pz, bestPc, bestRt);
        out.score = ck.matched;
        out.borderBad = ck.borderBad;
        out.cost = best;
    }
    return out;
}

// ---------------------------------------------------------------------------
SearchOutcome SearchParallel(const Puzzle& pz, const SearchParams& params, int threads,
                             const std::vector<int>* startPc,
                             const std::vector<int>* startRt) {
    threads = std::max(1, threads);
    std::vector<SearchOutcome> outs(threads);
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) {
        SearchParams p = params;
        p.seed = params.seed + (uint64_t)t * 7919ULL + 13ULL;
        ts.emplace_back([&pz, p, t, &outs, startPc, startRt]() {
            outs[t] = SearchOnce(pz, p, startPc, startRt);
        });
    }
    for (auto& th : ts) th.join();

    int bi = 0;
    for (int t = 1; t < threads; ++t) if (outs[t].cost < outs[bi].cost) bi = t;
    SearchOutcome best = outs[bi];
    // Aggregate stats across threads so the report is honest about total work.
    SearchStats agg;
    for (int t = 0; t < threads; ++t) {
        const SearchStats& s = outs[t].stats;
        agg.iterations += s.iterations;
        agg.restarts += s.restarts;
        agg.blossomPasses += s.blossomPasses;
        agg.blossomImproved += s.blossomImproved;
        agg.blossomReverted += s.blossomReverted;
        agg.blossomTotalPredicted += s.blossomTotalPredicted;
        agg.blossomTotalActual += s.blossomTotalActual;
        agg.assignPasses += s.assignPasses;
        agg.assignImproved += s.assignImproved;
        agg.assignTotalActual += s.assignTotalActual;
        agg.regionRuns += s.regionRuns;
        agg.regionImproved += s.regionImproved;
        agg.regionNodes += s.regionNodes;
        agg.regionBoundCalls += s.regionBoundCalls;
        agg.regionBoundPrunes += s.regionBoundPrunes;
        agg.coarseRotateSweeps += s.coarseRotateSweeps;
        agg.coarseRotates += s.coarseRotates;
        agg.coarseRotateGain += s.coarseRotateGain;
        agg.coarseSwapPasses += s.coarseSwapPasses;
        agg.coarseSwapImproved += s.coarseSwapImproved;
        agg.coarseSwapPredicted += s.coarseSwapPredicted;
        agg.coarseSwapActual += s.coarseSwapActual;
        agg.coarseSwapPairs += s.coarseSwapPairs;
        agg.rotStripSweeps += s.rotStripSweeps;
        agg.rotStripApplied += s.rotStripApplied;
        agg.rotStripGain += s.rotStripGain;
        agg.rotBoundCalls += s.rotBoundCalls;
        agg.rotBoundTriggers += s.rotBoundTriggers;
        agg.rotBoundBest = std::min(agg.rotBoundBest, s.rotBoundBest);
        agg.subSolveRuns += s.subSolveRuns;
        agg.subSolveImproved += s.subSolveImproved;
        agg.subSolveExact += s.subSolveExact;
        agg.subSolveNodes += s.subSolveNodes;
        agg.subSolveStubCalls += s.subSolveStubCalls;
        agg.subSolveStubPrunes += s.subSolveStubPrunes;
        agg.seconds += s.seconds;
        agg.matching.calls += s.matching.calls;
        agg.matching.totalVertices += s.matching.totalVertices;
        agg.matching.totalEdges += s.matching.totalEdges;
        agg.matching.seconds += s.matching.seconds;
        agg.matching.fallbackCalls += s.matching.fallbackCalls;
        if (t != bi) {
            // keep the winning thread's own fields, merge the rest
        }
    }
    best.stats = agg;
    return best;
}

// ---------------------------------------------------------------------------
bool SaveBoard(const std::string& path, const Puzzle& pz, const std::vector<int>& pc,
               const std::vector<int>& rt, const std::string& comment) {
    std::ofstream out(path);
    if (!out) return false;
    BoardCheck ck = CheckBoard(pz, pc, rt);
    out << "# Eternity-Blossom board\n";
    out << "# board " << pz.W << "x" << pz.H << " pieces " << pz.pieces.size() << "\n";
    out << "# matched " << ck.matched << "/" << pz.numEdges()
        << " border_bad " << ck.borderBad << " valid " << (ck.valid ? 1 : 0) << "\n";
    if (!comment.empty()) out << "# " << comment << "\n";
    for (size_t i = 0; i < pc.size(); ++i) out << pc[i] << " " << rt[i] << "\n";
    return true;
}

bool LoadBoard(const std::string& path, std::vector<int>& pc, std::vector<int>& rt,
               std::string* comment) {
    std::ifstream in(path);
    if (!in) return false;
    pc.clear(); rt.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') { if (comment) *comment += line + "\n"; continue; }
        std::istringstream ss(line);
        int p = 0, r = 0;
        if (!(ss >> p >> r)) return false;
        pc.push_back(p); rt.push_back(r);
    }
    return !pc.empty();
}

}  // namespace e2
