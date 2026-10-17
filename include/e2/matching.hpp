// Eternity-Blossom -- bridge to Blossom VI (minimum-weight perfect matching).
//
// SPDX-License-Identifier: MIT
//
// Blossom VI (Arkhipov & Kolmogorov, arXiv:2604.20351) is a C++ implementation
// of Edmonds' blossom algorithm for MINIMUM WEIGHT PERFECT MATCHING on a general
// -- i.e. non-bipartite -- graph.  That is exactly the primitive this project
// needs, because every relaxation of Eternity II that we use turns into a
// matching problem on a graph that contains odd cycles (triangles of
// mutually-compatible piece edges), and therefore cannot be handled by the
// bipartite / assignment machinery (Hungarian, min-cost flow) that most puzzle
// solvers reach for.
//
// Two entry points:
//
//   MinWeightPerfectMatching(n, edges)  -- the raw primitive, handed to Blossom VI.
//   MaxWeightMatching(n, edges)         -- maximum-weight *cardinality-free*
//                                         matching, via the standard dummy-clique
//                                         reduction (see .cpp).
//
// If the project is built without Blossom VI (-DE2_USE_BLOSSOM_VI=OFF) the
// MIT-only fallback in matching.cpp is used: an exact subset DP for n <= 18 and
// a greedy + 2-opt heuristic above that.  Correct, but exponential/slow -- the
// fallback exists so the repository is usable (and testable) on its own; all
// published experiments use Blossom VI.

#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <vector>

namespace e2 {

// One undirected weighted edge.  Weights are integers; Blossom VI requires
// |weight| <= 2^29-1 and even vertex counts with no isolated vertices.
using WEdge = std::tuple<int, int, int>;

struct MatchingStats {
    long long calls = 0;
    long long totalVertices = 0;
    long long totalEdges = 0;
    double seconds = 0.0;
    long long fallbackCalls = 0;  // solved without Blossom VI
};

// Minimum-weight perfect matching.  Returns false if the graph has no perfect
// matching (or the solver failed); on success `mate[v]` is v's partner and
// `weight` is the total weight.
bool MinWeightPerfectMatching(int n, const std::vector<WEdge>& edges,
                              std::vector<int>& mate, long long& weight,
                              MatchingStats* stats = nullptr);

// Maximum-weight matching (a matching of any cardinality maximising the total
// weight of its edges).  Returns the chosen pairs.  Uses Blossom VI through the
// dummy-clique reduction described in the .cpp.
bool MaxWeightMatching(int n, const std::vector<WEdge>& edges,
                       std::vector<std::pair<int, int>>& pairs, long long& weight,
                       MatchingStats* stats = nullptr);

// The dual certificate Blossom VI produces alongside a matching: the LAMINAR
// FAMILY OF BLOSSOMS (nested odd sets) with their dual variables, together with
// the primal and dual objectives.
//
// This is the part of the machinery that only exists because the graphs are not
// bipartite.  A bipartite matching needs no blossoms, so its certificate is
// just vertex duals; every entry here with a positive dual is a blossom that
// Edmonds' algorithm had to contract, i.e. a place where an odd set of vertices
// could not be matched internally.  Counting them answers, per instance, the
// question "was the non-bipartite machinery actually load-bearing, or would an
// assignment solver have done?"  -- see `eternity-blossom certify`.
struct MatchingCertificate {
    bool valid = false;         // the certificate was produced and primal == dual
    long long primal = 0;       // weight of the matching that was found
    long long dual = 0;         // objective of the dual solution
    // One entry per live blossom: (certificate index, dual*4, parent index or -1).
    std::vector<std::tuple<int, int, int>> blossoms;
    int blossomCount = 0;       // live blossoms, including singleton vertices
    int oddBlossoms = 0;        // blossoms carrying a strictly positive dual
    int maxDepth = 0;           // nesting depth of the blossom forest
};

// As MinWeightPerfectMatching, but additionally ask Blossom VI for its dual
// certificate.  Costs a little extra work inside the solver.
bool MinWeightPerfectMatchingCertified(int n, const std::vector<WEdge>& edges,
                                       std::vector<int>& mate, long long& weight,
                                       MatchingCertificate& cert,
                                       MatchingStats* stats = nullptr);

// True if this build is linked against Blossom VI.
bool HaveBlossomVI();

// True if MinWeightPerfectMatching is guaranteed to return a proven optimum.
// The MIT-only fallback is an exact subset DP only up to 18 vertices and a
// greedy + 2-opt heuristic above that, so with Blossom VI absent the answer is
// no -- and callers that need a *bound* (rather than a good matching) must not
// trust the result.  Using a heuristic matching as an admissible bound silently
// prunes away real solutions.
bool MatchingEngineIsExact();

// Human readable name of the matching engine in use.
std::string MatchingEngineName();

}  // namespace e2
