#pragma once

#include "init_method.h"
#include "rmp_model.h"
#include "shortest_path.h"
#include "stats.h"
#include "instance.h"

namespace cg {

class TwoPhaseMethod : public InitMethod {
public:
    TwoPhaseMethod(const Instance& instance, const RunConfig& config);

    InitMethodResult Run() override;
    const Stats& stats() const override { return stats_; }
    void SetConfig(const RunConfig& config) override { config_ = config; }

private:
    enum class Phase { kPhase1, kPhase2 };

    const Instance& instance_;
    RunConfig config_;
    RmpModel rmp_;
    ShortestPath shortest_path_;
    Stats stats_;

    Phase phase_ = Phase::kPhase1;
    Real phase1_objective_ = 0.0;
    std::vector<int> artificial_cols_;

    PricingOutput run_pricing_phase1(const std::vector<Real>& demand_duals);
    PricingOutput run_pricing_phase2(const std::vector<Real>& demand_duals,
                                     const std::vector<Real>& capacity_duals);
    bool check_phase1_optimality(const SolveResult& result);
    bool check_phase1_infeasibility(const SolveResult& result);
    bool check_feasibility(const SolveResult& result);
    void transition_to_phase2();
};

} // namespace cg