#include "shortest_path.h"
#include <queue>
#include <algorithm>
#include <stdexcept>

namespace cg {

using NodeDist = std::pair<Real, NodeId>;

DijkstraResult ShortestPath::solve(NodeId source, const std::vector<Real>& weights) const {
    auto start = std::chrono::high_resolution_clock::now();

    int32_t n = graph_.num_nodes();
    std::vector<Real> dist(n, kInf);
    std::vector<ArcId> prev_arc(n, -1);
    std::vector<int8_t> visited(n, 0);
    int64_t relaxations = 0;

    dist[source] = 0.0;
    std::priority_queue<NodeDist, std::vector<NodeDist>, std::greater<NodeDist>> pq;
    pq.emplace(0.0, source);

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (visited[u]) continue;
        visited[u] = 1;

        if (d > dist[u] + kEps) continue;

        for (ArcId aid : graph_.out_arcs(u)) {
            const Arc& arc = graph_.arc(aid);
            Real w = weights[aid];
            if (w < -kEps) {
                throw std::runtime_error("Negative weight encountered in Dijkstra: weights must be nonnegative");
            }
            Real nd = d + w;
            if (nd + kEps < dist[arc.head]) {
                dist[arc.head] = nd;
                prev_arc[arc.head] = aid;
                pq.emplace(nd, arc.head);
                ++relaxations;
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();

    DijkstraResult result;
    result.distance = std::move(dist);
    result.prev_arc = std::move(prev_arc);
    result.relaxations = relaxations;
    result.duration = end - start;
    return result;
}

Path ShortestPath::find_path(NodeId source, NodeId target, const std::vector<Real>& weights) const {
    DijkstraResult result = solve(source, weights);
    if (result.distance[target] >= kInf / 2) {
        return Path{};
    }
    return graph_.reconstruct_path(source, target, result.distance, result.prev_arc);
}

} // namespace cg