#pragma once

#include "lp_solver.h"
#include "init_method.h"
#include <vector>
#include <chrono>
#include <string>

namespace cg {

struct IterationLog {
    int iteration = 0;
    std::string phase;
    std::string lp_status;
    Real objective = 0.0;
    int64_t simplex_iterations = 0;
    std::chrono::nanoseconds master_time;
    std::chrono::nanoseconds pricing_time;
    int num_data_cols_added = 0;
    int num_dummy_cols_added = 0;
    int num_artificial_cols_added = 0;
    int total_rmp_cols = 0;
};

class Stats {
public:
    void record_lp_solve(const SolveResult& result);
    void record_pricing(const PricingOutput& pricing);
    void record_iteration(const IterationLog& log) { iteration_logs_.push_back(log); }
    void set_method_name(const std::string& name) { method_name_ = name; }
    void set_instance_name(const std::string& name) { instance_name_ = name; }
    void set_config(const RunConfig& config);
    void set_termination(const InitMethodResult& result);

    const std::vector<IterationLog>& iteration_logs() const { return iteration_logs_; }
    const std::string& method_name() const { return method_name_; }
    const std::string& instance_name() const { return instance_name_; }
    const RunConfig& config() const { return config_; }
    const InitMethodResult& termination() const { return termination_; }

    int64_t total_simplex_iterations() const { return total_simplex_iterations_; }
    std::chrono::nanoseconds total_master_time() const { return total_master_time_; }
    std::chrono::nanoseconds total_pricing_time() const { return total_pricing_time_; }
    int total_pricing_calls() const { return total_pricing_calls_; }
    int64_t total_relaxations() const { return total_relaxations_; }
    int rays_used() const { return rays_used_; }
    const std::vector<int>& ray_support_sizes() const { return ray_support_sizes_; }

    void add_ray_support(int support) { ray_support_sizes_.push_back(support); }
    void increment_rays_used() { ++rays_used_; }

private:
    std::string method_name_;
    std::string instance_name_;
    RunConfig config_;
    InitMethodResult termination_;

    std::vector<IterationLog> iteration_logs_;

    int64_t total_simplex_iterations_ = 0;
    std::chrono::nanoseconds total_master_time_;
    std::chrono::nanoseconds total_pricing_time_;
    int total_pricing_calls_ = 0;
    int64_t total_relaxations_ = 0;

    int rays_used_ = 0;
    std::vector<int> ray_support_sizes_;
};

} // namespace cg