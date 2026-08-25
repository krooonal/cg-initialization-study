#include "run_report.h"
#include "init_method.h"

namespace cg {

RunReport::RunReport(const Stats& stats) : stats_(stats) {}

JsonValue RunReport::to_json() const {
    JsonValue report;

    report["instance"] = stats_.instance_name();
    report["method"] = stats_.method_name();

    JsonValue config_obj;
    config_obj["M"] = stats_.config().M;
    config_obj["kappa"] = stats_.config().kappa;
    config_obj["pricing_tol"] = stats_.config().pricing_tol;
    config_obj["max_rounds"] = stats_.config().max_rounds;
    config_obj["max_extra_rounds"] = stats_.config().max_extra_rounds;
    config_obj["time_limit_sec"] = std::chrono::duration<double>(stats_.config().time_limit).count();
    config_obj["lp_time_limit_sec"] = std::chrono::duration<double>(stats_.config().lp_time_limit).count();
    config_obj["price_all_negative"] = stats_.config().price_all_negative;
    report["config"] = config_obj;

    JsonValue logs_array = JsonValue(JsonValue::Array{});
    for (const auto& log : stats_.iteration_logs()) {
        JsonValue log_obj;
        log_obj["iteration"] = log.iteration;
        log_obj["phase"] = log.phase;
        log_obj["lp_status"] = log.lp_status;
        log_obj["objective"] = log.objective;
        log_obj["simplex_iterations"] = log.simplex_iterations;
        log_obj["master_time_sec"] = std::chrono::duration<double>(log.master_time).count();
        log_obj["pricing_time_sec"] = std::chrono::duration<double>(log.pricing_time).count();
        log_obj["num_data_cols_added"] = log.num_data_cols_added;
        log_obj["num_dummy_cols_added"] = log.num_dummy_cols_added;
        log_obj["num_artificial_cols_added"] = log.num_artificial_cols_added;
        log_obj["total_rmp_cols"] = log.total_rmp_cols;
        logs_array.as_array().push_back(std::move(log_obj));
    }
    report["iteration_log"] = logs_array;

    const auto& term = stats_.termination();
    JsonValue endpoint;
    endpoint["status"] = [&]() -> std::string {
        switch (term.status) {
            case TerminationStatus::kFeasible: return "feasible";
            case TerminationStatus::kCertifiedInfeasible: return "certified_infeasible";
            case TerminationStatus::kBudgetExhausted: return "budget_exhausted";
            default: return "error";
        }
    }();
    endpoint["iterations_to_endpoint"] = term.iterations_to_endpoint;
    endpoint["final_objective"] = term.final_objective;
    endpoint["reference_objective"] = term.reference_objective;
    endpoint["gap"] = term.reference_objective > 0
        ? (term.final_objective - term.reference_objective) / std::abs(term.reference_objective)
        : 0.0;
    endpoint["master_infeasible"] = term.master_infeasible;
    report["endpoint"] = endpoint;

    JsonValue metrics;
    metrics["total_simplex_iterations"] = stats_.total_simplex_iterations();
    metrics["total_master_time_sec"] = std::chrono::duration<double>(stats_.total_master_time()).count();
    metrics["total_pricing_time_sec"] = std::chrono::duration<double>(stats_.total_pricing_time()).count();
    metrics["total_pricing_calls"] = stats_.total_pricing_calls();
    metrics["total_relaxations"] = stats_.total_relaxations();
    metrics["rays_used"] = stats_.rays_used();
    metrics["ray_support_sizes"] = JsonValue(JsonValue::Array{});
    for (int s : stats_.ray_support_sizes()) {
        metrics["ray_support_sizes"].as_array().push_back(JsonValue(s));
    }
    report["metrics"] = metrics;

    return report;
}

void RunReport::write_file(const std::string& filepath) const {
    to_json().write_file(filepath);
}

} // namespace cg