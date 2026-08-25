#pragma once

#include "graph.h"
#include <vector>
#include <queue>
#include <limits>
#include <chrono>

namespace cg {

struct DijkstraResult {
    std::vector<Real> distance;
    std::vector<ArcId> prev_arc;
    int64_t relaxations = 0;
    std::chrono::nanoseconds duration;
};

class ShortestPath {
public:
    explicit ShortestPath(const Graph& graph) : graph_(graph) {}

    DijkstraResult solve(NodeId source, const std::vector<Real>& weights) const;

    Path find_path(NodeId source, NodeId target, const std::vector<Real>& weights) const;

private:
    const Graph& graph_;
};

} // namespace cg