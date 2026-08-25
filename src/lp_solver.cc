#include "lp_solver.h"
#include <stdexcept>
#include "absl/time/time.h"

namespace cg {

using namespace operations_research::math_opt;

LpSolver::LpSolver(int num_rows,
                   const std::vector<std::pair<Real, Real>>& bounds)
    : num_rows_(num_rows) {
    constraints_.reserve(num_rows);
    for (int i = 0; i < num_rows; ++i) {
        Real lb = 0.0, ub = 0.0;
        if (i < static_cast<int>(bounds.size())) {
            lb = bounds[i].first;
            ub = bounds[i].second;
        }
        constraints_.push_back(model_.AddLinearConstraint(lb, ub, "c" + std::to_string(i)));
    }
}

LpSolver::~LpSolver() = default;

void LpSolver::init_row_bounds(const std::vector<std::pair<Real, Real>>& bounds) {
    if (static_cast<int>(bounds.size()) != num_rows_) {
        throw std::invalid_argument("Bounds size mismatch");
    }
    for (int i = 0; i < num_rows_; ++i) {
        model_.DeleteLinearConstraint(constraints_[i]);
        constraints_[i] = model_.AddLinearConstraint(bounds[i].first, bounds[i].second, "c" + std::to_string(i));
    }
}

void LpSolver::add_column(const std::vector<int>& row_indices,
                          const std::vector<Real>& coefficients,
                          Real objective_coeff,
                          Real lower_bound,
                          Real upper_bound) {
    if (row_indices.size() != coefficients.size()) {
        throw std::invalid_argument("Row indices and coefficients size mismatch");
    }

    // Use a finite upper bound to avoid issues with equality constraints
    constexpr Real kMaxFiniteBound = 1e9;
    Real effective_upper = (upper_bound > kMaxFiniteBound / 2) ? kMaxFiniteBound : upper_bound;
    
    Variable var = model_.AddVariable(lower_bound, effective_upper, false, "x" + std::to_string(num_cols_));
    variables_.push_back(var);

    for (size_t i = 0; i < row_indices.size(); ++i) {
        int row = row_indices[i];
        if (row < 0 || row >= num_rows_) {
            throw std::out_of_range("Row index out of range");
        }
        model_.set_coefficient(constraints_[row], var, coefficients[i]);
    }

    model_.set_objective_coefficient(var, objective_coeff);
    ++num_cols_;
}

void LpSolver::set_objective_coefficient(int col_idx, Real coeff) {
    if (col_idx < 0 || col_idx >= num_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    model_.set_objective_coefficient(variables_[col_idx], coeff);
}

Real LpSolver::get_objective_coefficient(int idx) const {
    if (idx < 0 || idx >= num_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    return model_.objective_coefficient(variables_[idx]);
}

void LpSolver::set_objective_sense_maximize() {
    model_.set_maximize();
}

void LpSolver::set_objective_sense_minimize() {
    model_.set_minimize();
}

void LpSolver::set_objective_linear_expression(const operations_research::math_opt::LinearExpression& expr, bool maximize) {
    if (maximize) {
        model_.Maximize(expr);
    } else {
        model_.Minimize(expr);
    }
}

operations_research::math_opt::LinearExpression LpSolver::get_objective_linear_expression() {
    return model_.ObjectiveAsLinearExpression();
}

void LpSolver::rebuild_objective_from_coefficients() {
    // Get the current objective as a linear expression and re-apply it
    auto obj_expr = model_.ObjectiveAsLinearExpression();
    if (model_.is_maximize()) {
        model_.Maximize(obj_expr);
    } else {
        model_.Minimize(obj_expr);
    }
}

void LpSolver::configure_parameters(SolveArguments& args,
                                    std::optional<std::chrono::nanoseconds> time_limit) {
    args.parameters.lp_algorithm = lp_algorithm_;
    args.parameters.glop.set_use_preprocessing(false);
    args.parameters.glop.set_use_scaling(false);

    if (time_limit.has_value()) {
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_limit.value());
        args.parameters.time_limit = absl::Seconds(seconds.count());
    }
}

SolveResult LpSolver::extract_result(const operations_research::math_opt::SolveResult& result) {
    SolveResult out;
    out.termination_reason = result.termination.ToString();

    switch (result.termination.reason) {
        case TerminationReason::kOptimal:
        case TerminationReason::kFeasible:
            out.status = SolveStatus::kFeasible;
            break;
        case TerminationReason::kInfeasible:
            out.status = SolveStatus::kInfeasible;
            break;
        case TerminationReason::kUnbounded:
            out.status = SolveStatus::kUnbounded;
            break;
        case TerminationReason::kInfeasibleOrUnbounded:
        case TerminationReason::kImprecise:
        case TerminationReason::kNoSolutionFound:
        case TerminationReason::kNumericalError:
        case TerminationReason::kOtherError:
        default:
            out.status = SolveStatus::kError;
    }

    if (out.status == SolveStatus::kFeasible) {
        out.primal_solution.resize(num_cols_);
        const auto& var_values = result.variable_values();
        for (int i = 0; i < num_cols_; ++i) {
            auto it = var_values.find(variables_[i]);
            out.primal_solution[i] = (it != var_values.end()) ? it->second : 0.0;
        }
        out.dual_solution.resize(num_rows_);
        const auto& dual_values = result.dual_values();
        for (int i = 0; i < num_rows_; ++i) {
            auto it = dual_values.find(constraints_[i]);
            out.dual_solution[i] = (it != dual_values.end()) ? it->second : 0.0;
        }
        if (result.has_primal_feasible_solution()) {
            out.objective_value = result.objective_value();
        } else {
            out.objective_value = 0.0;
        }
    } else if (out.status == SolveStatus::kInfeasible && result.has_dual_ray()) {
        const auto& ray_dual_values = result.ray_dual_values();
        out.dual_ray.resize(num_rows_);
        for (int i = 0; i < num_rows_; ++i) {
            auto it = ray_dual_values.find(constraints_[i]);
            out.dual_ray[i] = (it != ray_dual_values.end()) ? it->second : 0.0;
        }
    }

    const auto& stats = result.solve_stats;
    out.simplex_iterations = stats.simplex_iterations;
    out.solve_time = std::chrono::nanoseconds(absl::ToInt64Nanoseconds(stats.solve_time));

    return out;
}

SolveResult LpSolver::solve() {
    return solve_with_time_limit(std::chrono::nanoseconds::max());
}

SolveResult LpSolver::solve_with_time_limit(std::chrono::nanoseconds time_limit) {
    SolveArguments args;
    configure_parameters(args, time_limit);

    auto start = std::chrono::high_resolution_clock::now();
    absl::StatusOr<operations_research::math_opt::SolveResult> result = Solve(model_, SolverType::kGlop, args);
    auto end = std::chrono::high_resolution_clock::now();

    if (!result.ok()) {
        throw std::runtime_error("Solve failed: " + result.status().ToString());
    }

    SolveResult out = extract_result(*result);

    stats_.total_simplex_iterations += out.simplex_iterations;
    stats_.total_solve_time += (end - start);
    stats_.num_solves += 1;

    return out;
}

} // namespace cg