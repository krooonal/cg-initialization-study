#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <limits>
#include <optional>
#include <algorithm>

namespace cg {

using Real = double;
using NodeId = int32_t;
using ArcId = int32_t;
using CommodityId = int32_t;

constexpr Real kInf = std::numeric_limits<Real>::infinity();
constexpr Real kEps = 1e-9;

struct Arc {
    NodeId tail;
    NodeId head;
    Real capacity;
    Real cost;
    ArcId id;
};

struct Path {
    std::vector<ArcId> arcs;
    Real cost = 0.0;
    bool valid = false;
};

class Graph {
public:
    Graph() = default;
    explicit Graph(int32_t num_nodes) : num_nodes_(num_nodes), adj_(num_nodes) {}

    int32_t num_nodes() const { return num_nodes_; }
    int32_t num_arcs() const { return static_cast<int32_t>(arcs_.size()); }

    ArcId add_arc(NodeId tail, NodeId head, Real capacity, Real cost) {
        ArcId id = static_cast<ArcId>(arcs_.size());
        arcs_.push_back({tail, head, capacity, cost, id});
        adj_[tail].push_back(id);
        return id;
    }

    const Arc& arc(ArcId id) const { return arcs_[id]; }
    Arc& arc(ArcId id) { return arcs_[id]; }
    const std::vector<Arc>& arcs() const { return arcs_; }
    const std::vector<ArcId>& out_arcs(NodeId node) const { return adj_[node]; }

    bool has_arc(NodeId tail, NodeId head) const {
        for (ArcId id : adj_[tail]) {
            if (arcs_[id].head == head) return true;
        }
        return false;
    }

    std::optional<ArcId> find_arc(NodeId tail, NodeId head) const {
        for (ArcId id : adj_[tail]) {
            if (arcs_[id].head == head) return id;
        }
        return std::nullopt;
    }

    Real shortest_path_cost(NodeId source, NodeId target, const std::vector<Real>& weights) const {
        std::vector<Real> dist(num_nodes_, kInf);
        dist[source] = 0.0;
        std::vector<int8_t> visited(num_nodes_, 0);

        for (int32_t iter = 0; iter < num_nodes_; ++iter) {
            NodeId u = -1;
            Real best = kInf;
            for (NodeId v = 0; v < num_nodes_; ++v) {
                if (!visited[v] && dist[v] < best) {
                    best = dist[v];
                    u = v;
                }
            }
            if (u == -1 || u == target) break;
            visited[u] = 1;
            for (ArcId aid : adj_[u]) {
                const Arc& a = arcs_[aid];
                Real w = weights[aid];
                if (dist[u] + w < dist[a.head]) {
                    dist[a.head] = dist[u] + w;
                }
            }
        }
        return dist[target];
    }

    Path reconstruct_path(NodeId source, NodeId target,
                          const std::vector<Real>& dist,
                          const std::vector<ArcId>& prev_arc) const {
        Path path;
        if (dist[target] >= kInf / 2) return path;
        NodeId cur = target;
        while (cur != source) {
            ArcId aid = prev_arc[cur];
            if (aid < 0) return Path{};
            path.arcs.push_back(aid);
            cur = arcs_[aid].tail;
        }
        std::reverse(path.arcs.begin(), path.arcs.end());
        path.cost = dist[target];
        path.valid = true;
        return path;
    }

private:
    int32_t num_nodes_ = 0;
    std::vector<Arc> arcs_;
    std::vector<std::vector<ArcId>> adj_;
};

struct Commodity {
    CommodityId id;
    NodeId origin;
    NodeId destination;
    Real demand;
};

struct InstanceData {
    std::string name;
    std::string generator;
    int64_t seed = 0;
    std::string stratum;
    Graph graph;
    std::vector<Commodity> commodities;
    struct Reference {
        std::string status;
        std::optional<Real> objective;
    } reference;
};

} // namespace cg