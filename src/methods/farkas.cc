#include "methods/farkas.h"
#include <algorithm>
#include <chrono>
#include <vector>

namespace cg {

FarkasMethod::FarkasMethod(const Instance& instance, const RunConfig& config,
                         operations_research::math_opt::LPAlgorithm algo)
    : instance_(instance), config_(config), algo_(algo), rmp_(instance),
      shortest_path_(instance.graph()), stats_() {
    rmp_.solver().set_lp_algorithm(algo_);
    // Add initial columns from instance data
    const auto& init_cols = instance_.initial_columns();
    if (!init_cols.empty()) {
        if (config_.use_initial_columns) {
            // Add all initial columns if configured
            for (const auto& init_col : init_cols) {
                rmp_.add_data_column(init_col.commodity_id, init_col.arcs, init_col.cost);
            }
        } else {
            // Add only 1 initial column to seed GLOP's primal simplex basis
            const auto& init_col = init_cols[0];
            rmp_.add_data_column(init_col.commodity_id, init_col.arcs, init_col.cost);
        }
    }
}

std::vector<Real> FarkasMethod::normalize_ray(const std::vector<Real>& ray) const {
    std::vector<Real> normalized = ray;
    int num_demand = instance_.num_commodities();
    int num_capacity = instance_.graph().num_arcs();

    for (int e = 0; e < num_capacity; ++e) {
        int idx = num_demand + e;
        if (normalized[idx] < 0) {
            normalized[idx] = -normalized[idx];
        }
    }
    return normalized;
}

PricingOutput FarkasMethod::run_farkas_pricing(const std::vector<Real>& ray_values) {
    auto start = std::chrono::high_resolution_clock::now();
    PricingOutput output;

    std::vector<Real> normalized_ray = normalize_ray(ray_values);
    int num_demand = instance_.num_commodities();
    int num_capacity = instance_.graph().num_arcs();

    std::vector<Real> alpha(num_demand);
    std::vector<Real> v(num_capacity);

    for (int k = 0; k < num_demand; ++k) {
        alpha[k] = normalized_ray[k];
    }
    for (int e = 0; e < num_capacity; ++e) {
        v[e] = std::max(Real(0.0), normalized_ray[num_demand + e]);
    }

    for (int k = 0; k < num_demand; ++k) {
        const Commodity& comm = instance_.commodities()[k];
        Path path = shortest_path_.find_path(comm.origin, comm.destination, v);
        if (!path.valid) continue;

        Real cert_value = path.cost - alpha[k];
        if (cert_value < -config_.pricing_tol) {
            output.columns_to_add.emplace_back(k, std::move(path));
            if (!config_.price_all_negative) break;
        }
    }

    output.pricing_time = std::chrono::high_resolution_clock::now() - start;
    return output;
}

InitMethodResult FarkasMethod::Run() {
    InitMethodResult result;
    result.status = TerminationStatus::kError;
    int iteration = 0;

    while (iteration < config_.max_rounds) {
        ++iteration;
        SolveResult lp_result = rmp_.solver().solve_with_time_limit(config_.lp_time_limit);

        if (lp_result.basis.has_value()) {
            rmp_.solver().set_initial_basis(*lp_result.basis);
        }

        if (lp_result.status == SolveStatus::kFeasible) {
            stats_.record_lp_solve(lp_result);
            IterationLog log;
            log.iteration = iteration;
            log.phase = "farkas";
            log.lp_status = "feasible";
            log.objective = lp_result.objective_value;
            log.simplex_iterations = lp_result.simplex_iterations;
            log.master_time = lp_result.solve_time;
            log.total_rmp_cols = rmp_.columns().size();
            stats_.record_iteration(log);

            result.status = TerminationStatus::kFeasible;
            result.iterations_to_endpoint = iteration;
            result.final_objective = lp_result.objective_value;
            result.reference_objective = instance_.reference().objective.value_or(0.0);
            break;
        }

        if (lp_result.status == SolveStatus::kInfeasible || lp_result.status == SolveStatus::kUnbounded) {
            if (algo_ == operations_research::math_opt::LPAlgorithm::kPrimalSimplex) {
                // Extract dual ray via objective-zero dual simplex workaround
                std::vector<Real> orig_coeffs = rmp_.solver().get_all_objective_coefficients();
                rmp_.solver().zero_all_objective_coefficients();

                rmp_.solver().set_lp_algorithm(operations_research::math_opt::LPAlgorithm::kDualSimplex);
                SolveResult ray_solve_result = rmp_.solver().solve_with_time_limit(config_.lp_time_limit);

                lp_result.dual_ray = ray_solve_result.dual_ray;

                rmp_.solver().set_all_objective_coefficients(orig_coeffs);
                rmp_.solver().set_lp_algorithm(algo_);
            }
        } else {
            stats_.record_lp_solve(lp_result);
            result.status = TerminationStatus::kError;
            result.iterations_to_endpoint = iteration;
            break;
        }

        stats_.record_lp_solve(lp_result);

        if (!lp_result.dual_ray.empty()) {
            stats_.increment_rays_used();
            int support = 0;
            for (Real val : lp_result.dual_ray) {
                if (std::abs(val) > config_.pricing_tol) ++support;
            }
            stats_.add_ray_support(support);

            PricingOutput pricing = run_farkas_pricing(lp_result.dual_ray);
            stats_.record_pricing(pricing);

            IterationLog log;
            log.iteration = iteration;
            log.phase = "farkas";
            log.lp_status = "infeasible";
            log.objective = 0.0;
            log.simplex_iterations = lp_result.simplex_iterations;
            log.master_time = lp_result.solve_time;
            log.pricing_time = pricing.pricing_time;
            log.num_data_cols_added = pricing.columns_to_add.size();
            log.total_rmp_cols = rmp_.columns().size();
            stats_.record_iteration(log);

            if (pricing.columns_to_add.empty()) {
                result.status = TerminationStatus::kCertifiedInfeasible;
                result.iterations_to_endpoint = iteration;
                result.master_infeasible = true;
                break;
            }

            for (auto& [comm_id, path] : pricing.columns_to_add) {
                rmp_.add_data_column(comm_id, path.arcs, path.cost);
            }
        } else {
            result.status = TerminationStatus::kError;
            result.iterations_to_endpoint = iteration;
            break;
        }

        if (iteration >= config_.max_rounds) {
            result.status = TerminationStatus::kBudgetExhausted;
            result.iterations_to_endpoint = iteration;
            break;
        }
    }

    if (result.status == TerminationStatus::kFeasible) {
        for (int extra = 0; extra < config_.max_extra_rounds; ++extra) {
            SolveResult lp_result = rmp_.solver().solve_with_time_limit(config_.lp_time_limit);
            stats_.record_lp_solve(lp_result);

            if (lp_result.basis.has_value()) {
                rmp_.solver().set_initial_basis(*lp_result.basis);
            }

            if (lp_result.status != SolveStatus::kFeasible) break;

            int num_arcs = instance_.graph().num_arcs();
            std::vector<Real> weights(num_arcs);
            for (int e = 0; e < num_arcs; ++e) {
                Real w = instance_.graph().arc(e).cost - lp_result.dual_solution[rmp_.capacity_row_offset() + e];
                weights[e] = std::max(Real(0.0), w);
            }

            PricingOutput pricing;
            auto start = std::chrono::high_resolution_clock::now();
            for (int k = 0; k < instance_.num_commodities(); ++k) {
                const Commodity& comm = instance_.commodities()[k];
                Path path = shortest_path_.find_path(comm.origin, comm.destination, weights);
                if (!path.valid) continue;

                Real reduced_cost = path.cost - lp_result.dual_solution[k];
                if (reduced_cost < -config_.pricing_tol) {
                    pricing.columns_to_add.emplace_back(k, std::move(path));
                    if (!config_.price_all_negative) break;
                }
            }
            pricing.pricing_time = std::chrono::high_resolution_clock::now() - start;
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