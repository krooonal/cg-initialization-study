#pragma once

#include "init_method.h"
#include "rmp_model.h"
#include "shortest_path.h"
#include "stats.h"
#include "instance.h"

namespace cg {

class BigMMethod : public InitMethod {
public:
    BigMMethod(const Instance& instance, const RunConfig& config);

    InitMethodResult Run() override;
    const Stats& stats() const override { return stats_; }
    void SetConfig(const RunConfig& config) override { config_ = config; }

private:
    const Instance& instance_;
    RunConfig config_;
    RmpModel rmp_;
    ShortestPath shortest_path_;
    Stats stats_;

    Real compute_M() const;
    PricingOutput run_pricing(const std::vector<Real>& demand_duals,
                              const std::vector<Real>& capacity_duals);
    bool check_feasibility(const SolveResult& result);
    bool has_dummy_in_basis(const SolveResult& result) const;
};

} // namespace cg