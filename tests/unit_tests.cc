#include "shortest_path.h"
#include "graph.h"
#include "json.h"
#include "instance.h"
#include "lp_solver.h"
#include "methods/farkas.h"
#include <gtest/gtest.h>
#include <vector>

using namespace cg;

TEST(ShortestPathTest, SimpleGraph) {
    Graph g(4);
    g.add_arc(0, 1, 10.0, 1.0);
    g.add_arc(1, 2, 10.0, 2.0);
    g.add_arc(2, 3, 10.0, 3.0);
    g.add_arc(0, 2, 10.0, 5.0);
    g.add_arc(1, 3, 10.0, 10.0);

    ShortestPath sp(g);
    std::vector<Real> weights = {1.0, 2.0, 3.0, 5.0, 10.0};

    Path path = sp.find_path(0, 3, weights);
    EXPECT_TRUE(path.valid);
    EXPECT_EQ(path.arcs.size(), 3);
    EXPECT_NEAR(path.cost, 6.0, 1e-9);
}

TEST(ShortestPathTest, DisconnectedGraph) {
    Graph g(4);
    g.add_arc(0, 1, 10.0, 1.0);
    g.add_arc(2, 3, 10.0, 1.0);

    ShortestPath sp(g);
    std::vector<Real> weights = {1.0, 1.0};

    Path path = sp.find_path(0, 3, weights);
    EXPECT_FALSE(path.valid);
}

TEST(ShortestPathTest, NegativeWeightThrows) {
    Graph g(3);
    g.add_arc(0, 1, 10.0, 1.0);
    g.add_arc(1, 2, 10.0, 1.0);

    ShortestPath sp(g);
    std::vector<Real> weights = {-1.0, 1.0};

    EXPECT_THROW(sp.find_path(0, 2, weights), std::runtime_error);
}

TEST(ShortestPathTest, MultiplePaths) {
    Graph g(5);
    g.add_arc(0, 1, 10.0, 1.0);
    g.add_arc(1, 4, 10.0, 1.0);
    g.add_arc(0, 2, 10.0, 2.0);
    g.add_arc(2, 3, 10.0, 1.0);
    g.add_arc(3, 4, 10.0, 1.0);
    g.add_arc(0, 4, 10.0, 10.0);

    ShortestPath sp(g);
    std::vector<Real> weights = {1.0, 1.0, 2.0, 1.0, 1.0, 10.0};

    Path path = sp.find_path(0, 4, weights);
    EXPECT_TRUE(path.valid);
    EXPECT_NEAR(path.cost, 2.0, 1e-9);
}

TEST(GraphTest, AddArc) {
    Graph g(3);
    ArcId id = g.add_arc(0, 1, 5.0, 2.0);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(g.num_arcs(), 1);
    EXPECT_EQ(g.arc(id).tail, 0);
    EXPECT_EQ(g.arc(id).head, 1);
    EXPECT_EQ(g.arc(id).capacity, 5.0);
    EXPECT_EQ(g.arc(id).cost, 2.0);
}

TEST(GraphTest, OutArcs) {
    Graph g(3);
    g.add_arc(0, 1, 5.0, 2.0);
    g.add_arc(0, 2, 3.0, 1.0);
    g.add_arc(1, 2, 4.0, 3.0);

    auto out = g.out_arcs(0);
    EXPECT_EQ(out.size(), 2);
}

TEST(JsonTest, ParseObject) {
    std::string json_str = R"({"name": "test", "value": 42, "flag": true})";
    JsonValue json = JsonValue::parse(json_str);
    EXPECT_EQ(json.get<std::string>("name"), "test");
    EXPECT_EQ(json.get<int>("value"), 42);
    EXPECT_EQ(json.get<bool>("flag"), true);
}

TEST(JsonTest, ParseArray) {
    std::string json_str = "[1, 2, 3, 4]";
    JsonValue json = JsonValue::parse(json_str);
    EXPECT_EQ(json.as_array().size(), 4);
    EXPECT_EQ(json[0].as<int64_t>(), 1);
    EXPECT_EQ(json[3].as<int64_t>(), 4);
}

TEST(JsonTest, ParseNested) {
    std::string json_str = R"({"graph": {"nodes": 10, "arcs": [{"tail": 0, "head": 1}]}})";
    JsonValue json = JsonValue::parse(json_str);
    EXPECT_EQ(json["graph"]["nodes"].as<int64_t>(), 10);
    EXPECT_EQ(json["graph"]["arcs"][0]["tail"].as<int64_t>(), 0);
}

TEST(JsonTest, WriteRead) {
    JsonValue original;
    original["name"] = "test";
    original["value"] = 123;
    original["items"] = JsonValue(JsonValue::Array{});
    original["items"].as_array().push_back(JsonValue(1));
    original["items"].as_array().push_back(JsonValue(2));

    std::string dumped = original.dump();
    JsonValue parsed = JsonValue::parse(dumped);

    EXPECT_EQ(parsed.get<std::string>("name"), "test");
    EXPECT_EQ(parsed.get<int>("value"), 123);
    EXPECT_EQ(parsed["items"].as_array().size(), 2);
}

TEST(InstanceTest, LoadToyFeasible) {
    Instance inst = Instance::load("instances/toy_feasible.json");
    EXPECT_EQ(inst.name(), "toy_feasible");
    EXPECT_EQ(inst.num_nodes(), 4);
    EXPECT_EQ(inst.num_arcs(), 5);
    EXPECT_EQ(inst.num_commodities(), 1);
    EXPECT_EQ(inst.commodities()[0].demand, 5.0);
    EXPECT_TRUE(inst.reference().objective.has_value());
    EXPECT_NEAR(inst.reference().objective.value(), 15.0, 1e-9);
}

TEST(InstanceTest, LoadToyInfeasible) {
    Instance inst = Instance::load("instances/toy_infeasible.json");
    EXPECT_EQ(inst.name(), "toy_infeasible");
    EXPECT_EQ(inst.stratum(), "infeasible");
    EXPECT_FALSE(inst.reference().objective.has_value());
}

TEST(LpSolverTest, BasisAndObjectiveHelpers) {
    LpSolver solver(1, {{1.0, 1.0}});
    solver.add_column({0}, {1.0}, 5.0, 0.0, 10.0);
    solver.set_objective_sense_minimize();

    EXPECT_FALSE(solver.has_initial_basis());
    SolveResult res1 = solver.solve();
    EXPECT_EQ(res1.status, SolveStatus::kFeasible);
    EXPECT_TRUE(res1.basis.has_value());

    solver.set_initial_basis(*res1.basis);
    EXPECT_TRUE(solver.has_initial_basis());

    auto orig_coeffs = solver.get_all_objective_coefficients();
    EXPECT_EQ(orig_coeffs.size(), 1);
    EXPECT_DOUBLE_EQ(orig_coeffs[0], 5.0);

    solver.zero_all_objective_coefficients();
    EXPECT_DOUBLE_EQ(solver.get_objective_coefficient(0), 0.0);

    solver.set_all_objective_coefficients(orig_coeffs);
    EXPECT_DOUBLE_EQ(solver.get_objective_coefficient(0), 5.0);
}

TEST(FarkasMethodTest, ParametricAlgo) {
    Instance inst = Instance::load("instances/toy_infeasible.json");
    RunConfig config;
    config.use_initial_columns = true;

    FarkasMethod farkas_primal(inst, config, operations_research::math_opt::LPAlgorithm::kPrimalSimplex);
    InitMethodResult res_primal = farkas_primal.Run();
    EXPECT_EQ(res_primal.status, TerminationStatus::kCertifiedInfeasible);

    FarkasMethod farkas_dual(inst, config, operations_research::math_opt::LPAlgorithm::kDualSimplex);
    InitMethodResult res_dual = farkas_dual.Run();
    EXPECT_EQ(res_dual.status, TerminationStatus::kCertifiedInfeasible);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}