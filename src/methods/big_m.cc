#include "methods/big_m.h"
#include <algorithm>
#include <limits>
#include <chrono>

namespace cg {

BigMMethod::BigMMethod(const Instance& instance, const RunConfig& config)
    : instance_(instance), config_(config), rmp_(instance),
      shortest_path_(instance.graph()), stats_() {
    // Add initial columns from instance data if configured
    if (config_.use_initial_columns) {
        for (const auto& init_col : instance_.initial_columns()) {
            rmp_.add_data_column(init_col.commodity_id, init_col.arcs, init_col.cost);
        }
    }

    Real M = compute_M();
    for (int k = 0; k < instance_.num_commodities(); ++k) {
        rmp_.add_dummy_column(k, M);
    }
    rmp_.set_objective_minimize();
}

Real BigMMethod::compute_M() const {
    Real max_cost = 0.0;
    for (const Arc& arc : instance_.graph().arcs()) {
        max_cost = std::max(max_cost, std::abs(arc.cost));
    }
    return config_.kappa * max_cost;
}

PricingOutput BigMMethod::run_pricing(const std::vector<Real>& demand_duals,
                                      const std::vector<Real>& capacity_duals) {
    auto start = std::chrono::high_resolution_clock::now();
    PricingOutput output;

    int num_arcs = instance_.graph().num_arcs();
    std::vector<Real> weights(num_arcs);
    for (int e = 0; e < num_arcs; ++e) {
        // Reduced cost weight: w_e = c_e - β_e
        // For <= constraints in min problem, β_e <= 0, so w_e = c_e - β_e >= c_e >= 0
        // But solver might return opposite sign, so ensure non-negative
        Real weight = instance_.graph().arc(e).cost - capacity_duals[rmp_.capacity_row_offset() + e];
        weights[e] = std::max(weight, Real(0.0));
    }

    for (int k = 0; k < instance_.num_commodities(); ++k) {
        const Commodity& comm = instance_.commodities()[k];
        Path path = shortest_path_.find_path(comm.origin, comm.destination, weights);
        if (!path.valid) continue;

        Real reduced_cost = path.cost - demand_duals[k];
        if (reduced_cost < -config_.pricing_tol) {
            output.columns_to_add.emplace_back(k, std::move(path));
            if (!config_.price_all_negative) break;
        }
    }

    output.pricing_time = std::chrono::high_resolution_clock::now() - start;
    return output;
}

bool BigMMethod::check_feasibility(const SolveResult& result) {
    if (result.status != SolveStatus::kFeasible) return false;

    for (size_t i = 0; i < rmp_.columns().size(); ++i) {
        const ColumnInfo& col = rmp_.column_info(i);
        if (col.type == ColumnType::kDummy) {
            if (result.primal_solution[i] > config_.pricing_tol) {
                return false;
            }
        }
    }
    return true;
}

bool BigMMethod::has_dummy_in_basis(const SolveResult& result) const {
    if (result.status != SolveStatus::kFeasible) return false;
    for (size_t i = 0; i < rmp_.columns().size(); ++i) {
        const ColumnInfo& col = rmp_.column_info(i);
        if (col.type == ColumnType::kDummy) {
            if (result.primal_solution[i] > config_.pricing_tol) {
                return true;
            }
        }
    }
    return false;
}

InitMethodResult BigMMethod::Run() {
    InitMethodResult result;
    result.status = TerminationStatus::kError;
    int iteration = 0;
    int dummy_in_basis_count = 0;
    constexpr int kMaxDummyIterations = 20;

    while (iteration < config_.max_rounds) {
        ++iteration;
        auto lp_start = std::chrono::high_resolution_clock::now();
        SolveResult lp_result = rmp_.solver().solve_with_time_limit(config_.lp_time_limit);
        stats_.record_lp_solve(lp_result);

        if (lp_result.status == SolveStatus::kInfeasible) {
            result.status = TerminationStatus::kCertifiedInfeasible;
            result.master_infeasible = true;
            break;
        }

        if (lp_result.status != SolveStatus::kFeasible) {
            result.status = TerminationStatus::kError;
            break;
        }

        if (check_feasibility(lp_result)) {
            result.status = TerminationStatus::kFeasible;
            result.iterations_to_endpoint = iteration;
            result.final_objective = lp_result.objective_value;
            result.reference_objective = instance_.reference().objective.value_or(0.0);
            break;
        }

        if (has_dummy_in_basis(lp_result)) {
            ++dummy_in_basis_count;
            if (dummy_in_basis_count >= kMaxDummyIterations) {
                result.status = TerminationStatus::kCertifiedInfeasible;
                result.master_infeasible = true;
                break;
            }
        } else {
            dummy_in_basis_count = 0;
        }

        PricingOutput pricing = run_pricing(lp_result.dual_solution, lp_result.dual_solution);
        stats_.record_pricing(pricing);

        if (pricing.columns_to_add.empty()) {
            if (has_dummy_in_basis(lp_result)) {
                result.status = TerminationStatus::kCertifiedInfeasible;
                result.master_infeasible = true;
                break;
            }
        }

        for (auto& [comm_id, path] : pricing.columns_to_add) {
            rmp_.add_data_column(comm_id, path.arcs, path.cost);
        }

        if (iteration >= config_.max_rounds) {
            result.status = TerminationStatus::kBudgetExhausted;
            break;
        }
    }

    if (result.status == TerminationStatus::kFeasible) {
        for (int extra = 0; extra < config_.max_extra_rounds; ++extra) {
            SolveResult lp_result = rmp_.solver().solve_with_time_limit(config_.lp_time_limit);
            stats_.record_lp_solve(lp_result);

            PricingOutput pricing = run_pricing(lp_result.dual_solution, lp_result.dual_solution);
            stats_.record_pricing(pricing);

            if (pricing.columns_to_add.empty()) break;
            for (auto& [comm_id, path] : pricing.columns_to_add) {
                rmp_.add_data_column(comm_id, path.arcs, path.cost);
            }
        }
    }

    return result;
}

} // namespace cg