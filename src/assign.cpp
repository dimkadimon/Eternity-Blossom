// Eternity-Blossom -- exact bipartite assignment.  See assign.hpp.
// SPDX-License-Identifier: MIT

#include "e2/assign.hpp"

#include <algorithm>
#include <limits>

namespace e2 {

long long HungarianMin(const std::vector<std::vector<int>>& a, std::vector<int>* outColForRow) {
    const int n = (int)a.size();
    if (n == 0) return 0;
    const long long INF = std::numeric_limits<long long>::max() / 4;
    std::vector<long long> u(n + 1, 0), v(n + 1, 0), minv(n + 1);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::fill(minv.begin(), minv.end(), INF);
        std::vector<char> used(n + 1, 0);
        do {
            used[j0] = 1;
            const int i0 = p[j0];
            long long delta = INF;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const long long cur = (long long)a[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    if (outColForRow) {
        outColForRow->assign(n, -1);
        for (int j = 1; j <= n; ++j) (*outColForRow)[p[j] - 1] = j - 1;
    }
    long long total = 0;
    for (int j = 1; j <= n; ++j) total += a[p[j] - 1][j - 1];
    return total;
}

}  // namespace e2
