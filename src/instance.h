#pragma once

#include "graph.h"
#include "json.h"
#include <string>
#include <vector>
#include <optional>

namespace cg {

struct InitialColumn {
    CommodityId commodity_id;
    std::vector<ArcId> arcs;
    Real cost;
};

class Instance {
public:
    static Instance load(const std::string& filepath);

    const std::string& name() const { return name_; }
    const std::string& generator() const { return generator_; }
    int64_t seed() const { return seed_; }
    const std::string& stratum() const { return stratum_; }
    const Graph& graph() const { return graph_; }
    const std::vector<Commodity>& commodities() const { return commodities_; }
    const std::vector<InitialColumn>& initial_columns() const { return initial_columns_; }
    const InstanceData::Reference& reference() const { return reference_; }

    int32_t num_nodes() const { return graph_.num_nodes(); }
    int32_t num_arcs() const { return graph_.num_arcs(); }
    int32_t num_commodities() const { return static_cast<int32_t>(commodities_.size()); }

private:
    Instance() = default;
    std::string name_;
    std::string generator_;
    int64_t seed_ = 0;
    std::string stratum_;
    Graph graph_;
    std::vector<Commodity> commodities_;
    std::vector<InitialColumn> initial_columns_;
    InstanceData::Reference reference_;
};

} // namespace cg