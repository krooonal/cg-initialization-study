#include "stats.h"
#include "init_method.h"

namespace cg {

void Stats::record_lp_solve(const SolveResult& result) {
    total_simplex_iterations_ += result.simplex_iterations;
    total_master_time_ += result.solve_time;
}

void Stats::record_pricing(const PricingOutput& pricing) {
    total_pricing_time_ += pricing.pricing_time;
    total_pricing_calls_ += 1;
    total_relaxations_ += pricing.total_relaxations;
}

void Stats::set_config(const RunConfig& config) {
    config_ = config;
}

void Stats::set_termination(const InitMethodResult& result) {
    termination_ = result;
}

} // namespace cg