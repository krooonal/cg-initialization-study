#include "instance.h"
#include "column_generation.h"
#include "run_report.h"
#include <gtest/gtest.h>
#include <filesystem>

using namespace cg;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.pricing_tol = 1e-6;
        config_.max_rounds = 100;
        config_.max_extra_rounds = 5;
        config_.lp_time_limit = std::chrono::seconds(10);
        config_.use_initial_columns = true;
    }

    RunConfig config_;
};

TEST_F(IntegrationTest, FeasibleInstanceAllMethodsAgree) {
    Instance instance = Instance::load("instances/toy_feasible.json");

    auto big_m = create_method("big_m", instance, config_);
    auto two_phase = create_method("two_phase", instance, config_);
    auto farkas = create_method("farkas", instance, config_);

    InitMethodResult result_big_m = big_m->Run();
    InitMethodResult result_two_phase = two_phase->Run();
    InitMethodResult result_farkas = farkas->Run();

    EXPECT_EQ(result_big_m.status, TerminationStatus::kFeasible);
    EXPECT_EQ(result_two_phase.status, TerminationStatus::kFeasible);
    EXPECT_EQ(result_farkas.status, TerminationStatus::kFeasible);

    EXPECT_NEAR(result_big_m.final_objective, result_two_phase.final_objective, 1e-4);
    EXPECT_NEAR(result_big_m.final_objective, result_farkas.final_objective, 1e-4);

    EXPECT_TRUE(instance.reference().objective.has_value());
    Real ref_obj = instance.reference().objective.value();
    EXPECT_NEAR(result_big_m.final_objective, ref_obj, 1e-3);
    EXPECT_NEAR(result_two_phase.final_objective, ref_obj, 1e-3);
    EXPECT_NEAR(result_farkas.final_objective, ref_obj, 1e-3);
}

TEST_F(IntegrationTest, InfeasibleInstanceAllMethodsDetect) {
    Instance instance = Instance::load("instances/toy_infeasible.json");

    auto big_m = create_method("big_m", instance, config_);
    auto two_phase = create_method("two_phase", instance, config_);
    auto farkas = create_method("farkas", instance, config_);

    InitMethodResult result_big_m = big_m->Run();
    InitMethodResult result_two_phase = two_phase->Run();
    InitMethodResult result_farkas = farkas->Run();

    EXPECT_EQ(result_big_m.status, TerminationStatus::kCertifiedInfeasible);
    EXPECT_EQ(result_two_phase.status, TerminationStatus::kCertifiedInfeasible);
    EXPECT_EQ(result_farkas.status, TerminationStatus::kCertifiedInfeasible);

    EXPECT_TRUE(result_big_m.master_infeasible);
    EXPECT_TRUE(result_two_phase.master_infeasible);
    EXPECT_TRUE(result_farkas.master_infeasible);
}

TEST_F(IntegrationTest, WarmStartCheck) {
    Instance instance = Instance::load("instances/toy_infeasible.json");

    auto farkas = create_method("farkas", instance, config_);
    InitMethodResult result = farkas->Run();

    const Stats& stats = farkas->stats();
    int64_t total_iters = stats.total_simplex_iterations();
    int num_solves = stats.iteration_logs().size();

    EXPECT_GT(num_solves, 1);
    EXPECT_LT(total_iters, num_solves * 50);
}

TEST_F(IntegrationTest, ReportGeneration) {
    Instance instance = Instance::load("instances/toy_feasible.json");

    auto farkas = create_method("farkas", instance, config_);
    InitMethodResult result = farkas->Run();

    Stats& stats = const_cast<Stats&>(farkas->stats());
    stats.set_method_name("farkas");
    stats.set_instance_name(instance.name());
    stats.set_config(config_);
    stats.set_termination(result);

    RunReport report(stats);
    JsonValue json = report.to_json();

    EXPECT_EQ(json["instance"].as<std::string>(), "toy_feasible");
    EXPECT_EQ(json["method"].as<std::string>(), "farkas");
    EXPECT_TRUE(json.has_key("endpoint"));
    EXPECT_TRUE(json.has_key("metrics"));
    EXPECT_TRUE(json.has_key("iteration_log"));

    std::string output_file = "results/test_report.json";
    report.write_file(output_file);
    EXPECT_TRUE(std::filesystem::exists(output_file));
}

TEST_F(IntegrationTest, MParameterSweep) {
    Instance instance = Instance::load("instances/toy_feasible.json");

    for (Real M : {10.0, 100.0, 1000.0, 10000.0}) {
        RunConfig test_config = config_;
        test_config.M = M;
        test_config.kappa = M / 10.0;

        auto big_m = create_method("big_m", instance, test_config);
        InitMethodResult result = big_m->Run();

        EXPECT_EQ(result.status, TerminationStatus::kFeasible);
        EXPECT_GT(result.iterations_to_endpoint, 0);
    }
}

TEST_F(IntegrationTest, FarkasRaySupport) {
    Instance instance = Instance::load("instances/toy_infeasible.json");

    auto farkas = create_method("farkas", instance, config_);
    InitMethodResult result = farkas->Run();

    const Stats& stats = farkas->stats();
    EXPECT_GT(stats.rays_used(), 0);
    EXPECT_FALSE(stats.ray_support_sizes().empty());

    for (int support : stats.ray_support_sizes()) {
        EXPECT_GT(support, 0);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}