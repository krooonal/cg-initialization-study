#include "instance.h"
#include <stdexcept>

namespace cg {

Instance Instance::load(const std::string& filepath) {
    JsonValue json = JsonValue::parse_file(filepath);
    Instance inst;

    inst.name_ = json.get<std::string>("name", "");
    inst.generator_ = json.get<std::string>("generator", "");
    inst.seed_ = json.get<int64_t>("seed", 0);
    inst.stratum_ = json.get<std::string>("stratum", "");

    const JsonValue& graph_json = json["graph"];
    int32_t num_nodes = graph_json.get<int>("num_nodes", 0);
    inst.graph_ = Graph(num_nodes);

    const JsonValue& arcs_json = graph_json["arcs"];
    for (size_t i = 0; i < arcs_json.as_array().size(); ++i) {
        const JsonValue& a = arcs_json[i];
        NodeId tail = a.get<int>("tail", 0);
        NodeId head = a.get<int>("head", 0);
        Real capacity = a.get<Real>("capacity", 0.0);
        Real cost = a.get<Real>("cost", 0.0);
        inst.graph_.add_arc(tail, head, capacity, cost);
    }

    const JsonValue& commodities_json = json["commodities"];
    inst.commodities_.reserve(commodities_json.as_array().size());
    for (size_t i = 0; i < commodities_json.as_array().size(); ++i) {
        const JsonValue& c = commodities_json[i];
        Commodity commodity;
        commodity.id = static_cast<CommodityId>(i);
        commodity.origin = c.get<int>("origin", 0);
        commodity.destination = c.get<int>("destination", 0);
        commodity.demand = c.get<Real>("demand", 0.0);
        inst.commodities_.push_back(commodity);
    }

    // Parse initial_columns (optional, defaults to empty)
    if (json.has_key("initial_columns") && !json["initial_columns"].is_null()) {
        const JsonValue& init_cols_json = json["initial_columns"];
        inst.initial_columns_.reserve(init_cols_json.as_array().size());
        for (size_t i = 0; i < init_cols_json.as_array().size(); ++i) {
            const JsonValue& c = init_cols_json[i];
            InitialColumn init_col;
            init_col.commodity_id = c.get<int>("commodity", 0);
            for (const JsonValue& arc_json : c["arcs"].as_array()) {
                init_col.arcs.push_back(static_cast<int>(arc_json.as<int64_t>()));
            }
            init_col.cost = c.get<Real>("cost", 0.0);
            inst.initial_columns_.push_back(init_col);
        }
    }

    const JsonValue& ref_json = json["reference"];
    inst.reference_.status = ref_json.get<std::string>("status", "");
    if (ref_json.has_key("objective") && !ref_json["objective"].is_null()) {
        inst.reference_.objective = ref_json.get<Real>("objective", 0.0);
    }

    return inst;
}

} // namespace cg