#pragma once

#include "ortools/math_opt/cpp/math_opt.h"
#include "ortools/math_opt/cpp/variable_and_expressions.h"
#include <vector>
#include <optional>
#include <chrono>

namespace cg {

using Real = double;

enum class SolveStatus {
    kFeasible,
    kInfeasible,
    kUnbounded,
    kError,
    kTimeLimit
};

struct SolveResult {
    SolveStatus status;
    std::vector<Real> primal_solution;
    std::vector<Real> dual_solution;
    std::vector<Real> dual_ray;
    std::optional<operations_research::math_opt::Basis> basis;
    Real objective_value = 0.0;
    int64_t simplex_iterations = 0;
    std::chrono::nanoseconds solve_time;
    std::string termination_reason;
};

struct SolveStats {
    int64_t total_simplex_iterations = 0;
    std::chrono::nanoseconds total_solve_time;
    int num_solves = 0;
};

class LpSolver {
public:
    explicit LpSolver(int num_rows,
                      const std::vector<std::pair<Real, Real>>& bounds = {});
    ~LpSolver();

    void init_row_bounds(const std::vector<std::pair<Real, Real>>& bounds);

    void add_column(const std::vector<int>& row_indices,
                    const std::vector<Real>& coefficients,
                    Real objective_coeff,
                    Real lower_bound = 0.0,
                    Real upper_bound = 1e100);

    void set_objective_coefficient(int col_idx, Real coeff);

    void set_objective_sense_maximize();
    void set_objective_sense_minimize();

    void set_objective_linear_expression(const operations_research::math_opt::LinearExpression& expr, bool maximize);
    operations_research::math_opt::LinearExpression get_objective_linear_expression();
    void rebuild_objective_from_coefficients();

    std::vector<Real> get_all_objective_coefficients() const;
    void set_all_objective_coefficients(const std::vector<Real>& coeffs);
    void zero_all_objective_coefficients();

    void set_initial_basis(const operations_research::math_opt::Basis& basis) { initial_basis_ = basis; }
    void clear_initial_basis() { initial_basis_.reset(); }
    bool has_initial_basis() const { return initial_basis_.has_value(); }

    SolveResult solve();

    SolveResult solve_with_time_limit(std::chrono::nanoseconds time_limit = std::chrono::nanoseconds::max());

    const operations_research::math_opt::LinearConstraint& get_constraint(int idx) const { return constraints_[idx]; }
    
    Real get_coefficient(const operations_research::math_opt::LinearConstraint& c, const operations_research::math_opt::Variable& v) const {
        return model_.coefficient(c, v);
    }
    
    const operations_research::math_opt::Model& get_model() const { return model_; }
    
    int num_rows() const { return num_rows_; }
    int num_cols() const { return num_cols_; }

    const operations_research::math_opt::Variable& get_variable(int idx) const { return variables_[idx]; }
    Real get_objective_coefficient(int idx) const;

    void set_lp_algorithm(operations_research::math_opt::LPAlgorithm algo) { lp_algorithm_ = algo; }

    const SolveStats& stats() const { return stats_; }
    void reset_stats() { stats_ = SolveStats{}; }

private:
    int num_rows_;
    int num_cols_ = 0;

    operations_research::math_opt::LPAlgorithm lp_algorithm_ = operations_research::math_opt::LPAlgorithm::kPrimalSimplex;
    operations_research::math_opt::Model model_;
    std::unique_ptr<operations_research::math_opt::IncrementalSolver> incremental_solver_;
    std::vector<operations_research::math_opt::Variable> variables_;
    std::vector<operations_research::math_opt::LinearConstraint> constraints_;
    std::optional<operations_research::math_opt::Basis> initial_basis_;

    SolveStats stats_;

    void configure_parameters(operations_research::math_opt::SolveArguments& args,
                              std::optional<std::chrono::nanoseconds> time_limit);
    SolveResult extract_result(const operations_research::math_opt::SolveResult& result);
};

} // namespace cg