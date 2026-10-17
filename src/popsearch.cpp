// Eternity-Blossom -- memetic layer.  See popsearch.hpp.
// SPDX-License-Identifier: MIT

#include "e2/popsearch.hpp"

#include "e2/assign.hpp"

#include <algorithm>
#include <iostream>
#include <climits>
#include <limits>

namespace e2 {

bool CrossoverRegion(const Puzzle& pz,
                     const std::vector<int>& pcA, const std::vector<int>& rtA,
                     const std::vector<int>& pcB, const std::vector<int>& rtB,
                     int r0, int c0, int h, int w,
                     std::vector<int>& outPc, std::vector<int>& outRt,
                     const Board* ctx, bool repairAssign) {
    const int W = pz.W, n = pz.ncells;
    outPc = pcB;
    outRt = rtB;

    // which pieces A puts into the rectangle
    std::vector<char> inA(n, 0);
    for (int r = r0; r < r0 + h; ++r)
        for (int c = c0; c < c0 + w; ++c) inA[pcA[r * W + c]] = 1;

    // transplant
    for (int r = r0; r < r0 + h; ++r)
        for (int c = c0; c < c0 + w; ++c) {
            const int cell = r * W + c;
            outPc[cell] = pcA[cell];
            outRt[cell] = rtA[cell];
        }

    // Cells OUTSIDE the rectangle that now hold a duplicated piece must be
    // freed; pieces that were in the rectangle under B but are not in it under
    // A are unplaced and need a home.  The two sets are always the same size.
    std::vector<int> freed;
    std::vector<int> displaced;
    std::vector<int> dispRot;
    {
        std::vector<char> inRect(n * 4, 0);
        for (int r = r0; r < r0 + h; ++r)
            for (int c = c0; c < c0 + w; ++c) inRect[r * W + c] = 1;
        for (int cell = 0; cell < n; ++cell) {
            if (inRect[cell]) {
                if (!inA[pcB[cell]]) { displaced.push_back(pcB[cell]); dispRot.push_back(rtB[cell]); }
            } else {
                if (inA[pcB[cell]]) freed.push_back(cell);
            }
        }
    }
    if (freed.size() != displaced.size()) return false;

    if (!repairAssign || !ctx || freed.empty()) {
        for (size_t i = 0; i < freed.size(); ++i) {
            outPc[freed[i]] = displaced[i];
            outRt[freed[i]] = dispRot[i];
        }
        return true;
    }

    // Where should each displaced piece go?  Free choice of cell and of
    // rotation, so cost is a k x k matrix over "damage at that cell", and the
    // minimum total damage is a bipartite assignment -- exactly HungarianMin.
    // (Two freed cells can be adjacent, which makes the true cost slightly
    // non-additive; the child does not have to be exact, only good, and the
    // local search that follows measures the real board anyway.)
    const int k = (int)freed.size();
    std::vector<std::vector<int>> cm((size_t)k, std::vector<int>(k, 0));
    std::vector<std::vector<int>> bestRot((size_t)k, std::vector<int>(k, 0));
    for (int i = 0; i < k; ++i) {
        const int piece = displaced[i];
        for (int j = 0; j < k; ++j) {
            const int cell = freed[j];
            int best = INT_MAX, br = 0;
            for (int r = 0; r < 4; ++r) {
                int c = 0;
                for (int s = 0; s < 4; ++s) {
                    const int base = cell * 4 + s;
                    const int nb = ctx->pz->neigh[base];
                    const int wv = (nb < 0) ? ctx->wBorderSlot : 2;
                    const int nc = (int)ctx->protCol[piece * 16 + r * 4 + s];
                    if (nc != (int)ctx->target[base]) c += wv;
                }
                if (c < best) { best = c; br = r; }
            }
            cm[i][j] = best;
            bestRot[i][j] = br;
        }
    }
    std::vector<int> colForRow;
    HungarianMin(cm, &colForRow);
    for (int i = 0; i < k; ++i) {
        outPc[freed[colForRow[i]]] = displaced[i];
        outRt[freed[colForRow[i]]] = bestRot[i][colForRow[i]];
    }
    return true;
}

namespace {

struct Individual {
    std::vector<int> pc, rt;
    long long cost = 0;
    uint64_t seed = 1;
};

}  // namespace

PopulationOutcome PopulationSearch(const Puzzle& pz, const PopulationParams& params) {
    PopulationOutcome out;
    PopulationStats& st = out.stats;
    const auto T0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
    };

    std::mt19937 rng((unsigned)(params.seed * 2654435761u + 1u));
    const int P = std::max(2, params.popSize);
    const int E = std::min(params.elite, P - 1);

    // ---- 1. seed the population with independent short runs
    std::vector<Individual> pop(P);
    const double tInit0 = elapsed();
    for (int i = 0; i < P; ++i) {
        SearchParams sp = params.search;
        sp.seed = params.seed * 7919ull + (uint64_t)i * 104729ull + 1ull;
        sp.timeLimitSeconds = params.initSeconds;
        sp.verbose = false;
        sp.trace = false;
        SearchOutcome r = SearchOnce(pz, sp);
        pop[i].pc = r.pc;
        pop[i].rt = r.rt;
        pop[i].cost = r.cost;
        pop[i].seed = sp.seed;
        if (params.verbose)
            std::cout << "  init " << i << ": cost " << r.cost << " (" << r.score << "/480)\\n";
    }
    st.initSeconds = elapsed() - tInit0;

    auto sortPop = [&]() {
        std::sort(pop.begin(), pop.end(),
                  [](const Individual& a, const Individual& b) { return a.cost < b.cost; });
    };
    sortPop();

    Individual best = pop[0];
    auto noteBest = [&]() {
        if (pop[0].cost < best.cost) best = pop[0];
    };
    noteBest();

    auto uni = [&](int lo, int hi) {
        return lo + (int)(rng() % (uint64_t)(hi - lo + 1));
    };

    // ---- 2. generations
    const double tBreed0 = elapsed();
    while (elapsed() < params.timeLimitSeconds) {
        sortPop();
        st.generations++;

        for (int slot = E; slot < P; ++slot) {
            if (elapsed() >= params.timeLimitSeconds) break;

            std::vector<int> startPc, startRt;
            if (params.useCrossover) {
                // tournament selection for two distinct parents
                int a = -1, b = -1;
                for (int t = 0; t < params.tournament; ++t) {
                    const int c = (int)(rng() % (uint64_t)P);
                    if (a < 0 || pop[c].cost < pop[a].cost) a = c;
                }
                for (int t = 0; t < params.tournament; ++t) {
                    const int c = (int)(rng() % (uint64_t)P);
                    if (c == a) continue;
                    if (b < 0 || pop[c].cost < pop[b].cost) b = c;
                }
                if (b < 0) b = (a + 1) % P;

                const int h = uni(params.regionMin, params.regionMax);
                const int w = uni(params.regionMin, params.regionMax);
                const int r0 = (int)(rng() % (uint64_t)(pz.H - h + 1));
                const int c0 = (int)(rng() % (uint64_t)(pz.W - w + 1));
                Board ctx(pz);
                ctx.load(pop[b].pc, pop[b].rt);
                if (!CrossoverRegion(pz, pop[a].pc, pop[a].rt, pop[b].pc, pop[b].rt,
                                     r0, c0, h, w, startPc, startRt, &ctx,
                                     params.repairAssign)) {
                    startPc.clear();   // fall back to a fresh restart
                    startRt.clear();
                } else {
                    st.crossovers++;
                }
            }

            SearchParams sp = params.search;
            sp.seed = params.seed * 15485863ull + (uint64_t)st.generations * 104729ull +
                      (uint64_t)slot * 31ull + 7ull;
            sp.timeLimitSeconds = params.breedSeconds;
            sp.saT0 = params.breedT0;
            sp.saT1 = params.breedT1;
            sp.verbose = false;
            sp.trace = false;
            SearchOutcome r = SearchOnce(pz, sp,
                                         startPc.empty() ? nullptr : &startPc,
                                         startRt.empty() ? nullptr : &startRt);
            // how good was the child BEFORE refinement?  This is the whole
            // question for crossover: if a child starts far below both parents
            // then no amount of local search inside a short budget will rescue
            // it, and the crossover is just an expensive way to restart badly.
            if (!startPc.empty()) {
                Board cb(pz);
                cb.load(startPc, startRt);
                st.childCostSum += cb.cost;
                st.childCostMin = std::min(st.childCostMin, cb.cost);
                ++st.children;
            }
            if (r.cost < pop[slot].cost) {
                pop[slot].pc = r.pc;
                pop[slot].rt = r.rt;
                pop[slot].cost = r.cost;
                st.improvements++;
            }
        }
        sortPop();
        noteBest();

        if (params.trace || params.verbose) {
            double sum = 0;
            for (const Individual& in : pop) sum += in.cost;
            std::cout << "  gen " << st.generations << "  best " << pop[0].cost
                      << " (" << (480 - pop[0].cost / 2) << "/480)  mean "
                      << (int)(sum / P) << "  t=" << (int)elapsed() << "s\\n";
            std::cout.flush();
        }
    }
    st.breedSeconds = elapsed() - tBreed0;

    // ---- 3. result
    out.pc = best.pc;
    out.rt = best.rt;
    out.cost = best.cost;
    Board b(pz);
    b.load(best.pc, best.rt);
    out.cost = b.cost;
    BoardCheck ck = CheckBoard(pz, best.pc, best.rt);
    out.score = ck.matched;
    out.borderBad = ck.borderBad;
    st.bestCost = out.cost;
    st.seconds = elapsed();
    return out;
}

}  // namespace e2
