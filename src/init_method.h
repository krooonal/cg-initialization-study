#pragma once

#include "shortest_path.h"
#include <chrono>
#include <optional>
#include <vector>

namespace cg {

class Stats;

enum class TerminationStatus {
    kFeasible,
    kCertifiedInfeasible,
    kBudgetExhausted,
    kError
};

struct InitMethodResult {
    TerminationStatus status;
    int iterations_to_endpoint = 0;
    Real final_objective = 0.0;
    Real reference_objective = 0.0;
    bool master_infeasible = false;
};

struct RunConfig {
    Real M = 100.0;
    Real kappa = 100.0;
    Real pricing_tol = 1e-6;
    int max_rounds = 1000;
    int max_extra_rounds = 10;
    std::chrono::nanoseconds time_limit = std::chrono::seconds(300);
    std::chrono::nanoseconds lp_time_limit = std::chrono::seconds(30);
    bool price_all_negative = false;
    bool use_initial_columns = false;  // Default: start from 0 columns
};

class InitMethod {
public:
    virtual ~InitMethod() = default;
    virtual InitMethodResult Run() = 0;
    virtual const Stats& stats() const = 0;
    virtual void SetConfig(const RunConfig& config) = 0;
};

struct PricingInput {
    std::vector<Real> demand_duals;
    std::vector<Real> capacity_duals;
    std::vector<Real> ray_values;
    bool is_farkas = false;
};

struct PricingOutput {
    std::vector<std::pair<int, Path>> columns_to_add;
    int64_t total_relaxations = 0;
    std::chrono::nanoseconds pricing_time;
};

} // namespace cg