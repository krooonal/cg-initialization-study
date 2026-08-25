#pragma once

#include "instance.h"
#include "lp_solver.h"
#include <vector>
#include <string>
#include <algorithm>

namespace cg {

enum class ColumnType {
    kData,
    kDummy,
    kArtificial
};

struct ColumnInfo {
    ColumnType type;
    int commodity_id = -1;
    std::vector<int> arcs;
};

class RmpModel {
public:
    explicit RmpModel(const Instance& instance);

    int add_data_column(int commodity_id, const std::vector<int>& arcs, Real cost);
    int add_dummy_column(int commodity_id, Real cost);
    int add_artificial_column(int commodity_id);

    void set_artificial_objectives_zero();
    void set_dummy_objectives(Real M);
    void set_data_objectives_to_actual_costs();
    void set_objective_minimize();

    const LpSolver& solver() const { return solver_; }
    LpSolver& solver() { return solver_; }

    int num_demand_rows() const { return num_demand_rows_; }
    int num_capacity_rows() const { return num_capacity_rows_; }
    int demand_row_offset() const { return 0; }
    int capacity_row_offset() const { return num_demand_rows_; }

    const std::vector<ColumnInfo>& columns() const { return columns_; }
    ColumnInfo& column_info(int col_idx) { return columns_[col_idx]; }
    const ColumnInfo& column_info(int col_idx) const { return columns_[col_idx]; }

    int num_data_columns() const { return num_data_columns_; }
    int num_dummy_columns() const { return num_dummy_columns_; }
    int num_artificial_columns() const { return num_artificial_columns_; }

private:
    const Instance& instance_;
    LpSolver solver_;

    int num_demand_rows_;
    int num_capacity_rows_;

    std::vector<ColumnInfo> columns_;
    int num_data_columns_ = 0;
    int num_dummy_columns_ = 0;
    int num_artificial_columns_ = 0;

    void build_initial_model();
};

} // namespace cg