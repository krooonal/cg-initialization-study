#pragma once

#include "init_method.h"
#include "rmp_model.h"
#include "shortest_path.h"
#include "stats.h"
#include "instance.h"

namespace cg {

class FarkasMethod : public InitMethod {
public:
    FarkasMethod(const Instance& instance, const RunConfig& config);

    InitMethodResult Run() override;
    const Stats& stats() const override { return stats_; }
    void SetConfig(const RunConfig& config) override { config_ = config; }

private:
    const Instance& instance_;
    RunConfig config_;
    RmpModel rmp_;
    ShortestPath shortest_path_;
    Stats stats_;

    int rays_used_ = 0;
    std::vector<int> ray_support_sizes_;

    PricingOutput run_farkas_pricing(const std::vector<Real>& ray_values);
    std::vector<Real> normalize_ray(const std::vector<Real>& ray) const;
};

} // namespace cg