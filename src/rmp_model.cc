#include "rmp_model.h"
#include <stdexcept>

namespace cg {

RmpModel::RmpModel(const Instance& instance)
    : instance_(instance),
      solver_(instance.num_commodities() + instance.graph().num_arcs(),
              [&]() {
                  std::vector<std::pair<Real, Real>> bounds;
                  bounds.reserve(instance.num_commodities() + instance.graph().num_arcs());
                  for (int k = 0; k < instance.num_commodities(); ++k) {
                      const Commodity& comm = instance.commodities()[k];
                      bounds.emplace_back(comm.demand, comm.demand);
                  }
                  for (int e = 0; e < instance.graph().num_arcs(); ++e) {
                      const Arc& arc = instance.graph().arc(e);
                      bounds.emplace_back(0.0, arc.capacity);
                  }
                  return bounds;
              }()),
      num_demand_rows_(instance.num_commodities()),
      num_capacity_rows_(instance.graph().num_arcs()) {
}

int RmpModel::add_data_column(int commodity_id, const std::vector<int>& arcs, Real cost) {
    if (commodity_id < 0 || commodity_id >= num_demand_rows_) {
        throw std::out_of_range("Invalid commodity_id");
    }

    std::vector<int> row_indices;
    std::vector<Real> coefficients;

    row_indices.push_back(commodity_id);
    coefficients.push_back(1.0);

    for (int arc_id : arcs) {
        if (arc_id < 0 || arc_id >= num_capacity_rows_) {
            throw std::out_of_range("Invalid arc_id");
        }
        row_indices.push_back(capacity_row_offset() + arc_id);
        coefficients.push_back(1.0);
    }

    int col_idx = solver_.num_cols();
    solver_.add_column(row_indices, coefficients, cost);
    columns_.push_back({ColumnType::kData, commodity_id, arcs});
    ++num_data_columns_;
    return col_idx;
}

int RmpModel::add_dummy_column(int commodity_id, Real cost) {
    if (commodity_id < 0 || commodity_id >= num_demand_rows_) {
        throw std::out_of_range("Invalid commodity_id");
    }

    std::vector<int> row_indices = {commodity_id};
    std::vector<Real> coefficients = {1.0};

    int col_idx = solver_.num_cols();
    solver_.add_column(row_indices, coefficients, cost);
    columns_.push_back({ColumnType::kDummy, commodity_id, {}});
    ++num_dummy_columns_;
    return col_idx;
}

int RmpModel::add_artificial_column(int commodity_id) {
    if (commodity_id < 0 || commodity_id >= num_demand_rows_) {
        throw std::out_of_range("Invalid commodity_id");
    }

    std::vector<int> row_indices = {commodity_id};
    std::vector<Real> coefficients = {1.0};

    int col_idx = solver_.num_cols();
    solver_.add_column(row_indices, coefficients, 1.0);
    columns_.push_back({ColumnType::kArtificial, commodity_id, {}});
    ++num_artificial_columns_;
    return col_idx;
}

void RmpModel::set_artificial_objectives_zero() {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].type == ColumnType::kArtificial) {
            solver_.set_objective_coefficient(static_cast<int>(i), 0.0);
        }
    }
}

void RmpModel::set_dummy_objectives(Real M) {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].type == ColumnType::kDummy) {
            solver_.set_objective_coefficient(static_cast<int>(i), M);
        }
    }
}

void RmpModel::set_data_objectives_to_actual_costs() {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].type == ColumnType::kData) {
            // Compute actual cost from arcs
            Real cost = 0.0;
            for (int arc_id : columns_[i].arcs) {
                cost += instance_.graph().arc(arc_id).cost;
            }
            solver_.set_objective_coefficient(static_cast<int>(i), cost);
        }
    }
    // Rebuild objective expression and set it
    solver_.rebuild_objective_from_coefficients();
}

void RmpModel::set_objective_minimize() {
    solver_.set_objective_sense_minimize();
}

} // namespace cg