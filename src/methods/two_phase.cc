#include "methods/two_phase.h"
#include <algorithm>
#include <chrono>

namespace cg {

TwoPhaseMethod::TwoPhaseMethod(const Instance& instance, const RunConfig& config)
    : instance_(instance), config_(config), rmp_(instance),
      shortest_path_(instance.graph()), stats_() {
    // Add initial columns from instance data if configured
    if (config_.use_initial_columns) {
        for (const auto& init_col : instance_.initial_columns()) {
            rmp_.add_data_column(init_col.commodity_id, init_col.arcs, init_col.cost);
        }
    }

    for (int k = 0; k < instance_.num_commodities(); ++k) {
        int col = rmp_.add_artificial_column(k);
        artificial_cols_.push_back(col);
    }
    rmp_.set_objective_minimize();
}

PricingOutput TwoPhaseMethod::run_pricing_phase1(const std::vector<Real>& demand_duals) {
    auto start = std::chrono::high_resolution_clock::now();
    PricingOutput output;

    int num_arcs = instance_.graph().num_arcs();
    std::vector<Real> weights(num_arcs, 0.0);

    for (int k = 0; k < instance_.num_commodities(); ++k) {
        const Commodity& comm = instance_.commodities()[k];
        Path path = shortest_path_.find_path(comm.origin, comm.destination, weights);
        if (!path.valid) continue;

        Real reduced_cost = -demand_duals[k];
        if (reduced_cost < -config_.pricing_tol) {
            output.columns_to_add.emplace_back(k, std::move(path));
            if (!config_.price_all_negative) break;
        }
    }

    output.pricing_time = std::chrono::high_resolution_clock::now() - start;
    return output;
}

PricingOutput TwoPhaseMethod::run_pricing_phase2(const std::vector<Real>& demand_duals,
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

bool TwoPhaseMethod::check_phase1_optimality(const SolveResult& result) {
    if (result.status != SolveStatus::kFeasible) return false;
    phase1_objective_ = result.objective_value;
    // Return true if Phase 1 is optimal and we should transition or detect infeasibility
    return phase1_objective_ <= config_.pricing_tol;
}

bool TwoPhaseMethod::check_phase1_infeasibility(const SolveResult& result) {
    if (result.status != SolveStatus::kFeasible) return false;
    phase1_objective_ = result.objective_value;
    // Phase 1 is optimal but objective > tol -> infeasible
    return phase1_objective_ > config_.pricing_tol;
}

bool TwoPhaseMethod::check_feasibility(const SolveResult& result) {
    if (result.status != SolveStatus::kFeasible) return false;

    for (int col : artificial_cols_) {
        if (result.primal_solution[col] > config_.pricing_tol) {
            return false;
        }
    }
    return true;
}

void TwoPhaseMethod::transition_to_phase2() {
    rmp_.set_artificial_objectives_zero();
    rmp_.set_data_objectives_to_actual_costs();
    
    // Set artificial variables to a very high cost (Big-M) so they won't enter the basis
    constexpr Real kBigM = 1e9;
    for (int col : artificial_cols_) {
        rmp_.solver().set_objective_coefficient(col, kBigM);
    }
    
    // Rebuild objective
    rmp_.solver().rebuild_objective_from_coefficients();
    
    phase_ = Phase::kPhase2;
}

InitMethodResult TwoPhaseMethod::Run() {
    InitMethodResult result;
    result.status = TerminationStatus::kError;
    int iteration = 0;
    int phase1_iterations = 0;
    Real last_phase1_obj = std::numeric_limits<Real>::infinity();
    int phase1_no_improve = 0;
    constexpr int kMaxPhase1NoImprove = 20;

    while (iteration < config_.max_rounds) {
        ++iteration;
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

        if (phase_ == Phase::kPhase1) {
            ++phase1_iterations;
            Real current_obj = lp_result.objective_value;
            
            if (check_phase1_optimality(lp_result)) {
                transition_to_phase2();
                continue;
            } else {
                // Track Phase 1 objective improvement
                if (current_obj >= last_phase1_obj - config_.pricing_tol) {
                    ++phase1_no_improve;
                } else {
                    phase1_no_improve = 0;
                }
                last_phase1_obj = current_obj;

                // If Phase 1 objective hasn't improved for many iterations, check for infeasibility
                if (phase1_no_improve >= kMaxPhase1NoImprove) {
                    if (check_phase1_infeasibility(lp_result)) {
                        std::cerr << "DEBUG: Phase 1 infeasible detected (no improvement), objective=" << phase1_objective_ << std::endl;
                        result.status = TerminationStatus::kCertifiedInfeasible;
                        result.master_infeasible = true;
                        break;
                    }
                }

                PricingOutput pricing = run_pricing_phase1(lp_result.dual_solution);
                stats_.record_pricing(pricing);

                if (pricing.columns_to_add.empty()) {
                    if (check_phase1_infeasibility(lp_result)) {
                        std::cerr << "DEBUG: Phase 1 infeasible detected (no columns), objective=" << phase1_objective_ << std::endl;
                        result.status = TerminationStatus::kCertifiedInfeasible;
                        result.master_infeasible = true;
                        break;
                    }
                }

                for (auto& [comm_id, path] : pricing.columns_to_add) {
                    rmp_.add_data_column(comm_id, path.arcs, 0.0);
                }
                continue;
            }
        }
        if (phase_ == Phase::kPhase2) {
            if (check_feasibility(lp_result)) {
                result.status = TerminationStatus::kFeasible;
                result.iterations_to_endpoint = iteration;
                result.final_objective = lp_result.objective_value;
                result.reference_objective = instance_.reference().objective.value_or(0.0);
                break;
            }

            PricingOutput pricing = run_pricing_phase2(lp_result.dual_solution,
                                                       lp_result.dual_solution);
            stats_.record_pricing(pricing);

            if (pricing.columns_to_add.empty()) {
                result.status = TerminationStatus::kCertifiedInfeasible;
                result.master_infeasible = true;
                break;
            }

            for (auto& [comm_id, path] : pricing.columns_to_add) {
                rmp_.add_data_column(comm_id, path.arcs, path.cost);
            }
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

            PricingOutput pricing = run_pricing_phase2(lp_result.dual_solution,
                                                       lp_result.dual_solution);
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