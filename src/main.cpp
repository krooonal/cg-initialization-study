#include "instance.h"
#include "column_generation.h"
#include "run_report.h"
#include "stats.h"
#include <iostream>
#include <string>
#include <chrono>

using namespace cg;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <instance.json> <method> [options]\n"
              << "Methods: big_m, two_phase, farkas\n"
              << "Options:\n"
              << "  --M <value>              Big-M parameter (default: 100)\n"
              << "  --kappa <value>          Kappa parameter (default: 100)\n"
              << "  --tol <value>            Pricing tolerance (default: 1e-6)\n"
              << "  --max-rounds <n>         Maximum CG rounds (default: 1000)\n"
              << "  --extra-rounds <n>       Extra rounds after feasibility (default: 10)\n"
              << "  --time-limit <sec>       Total time limit (default: 300)\n"
              << "  --lp-time-limit <sec>    Per-LP time limit (default: 30)\n"
              << "  --price-all              Price all negative reduced cost columns\n"
              << "  --use-initial-columns    Use initial columns from instance JSON (default: false)\n"
              << "  -o, --output <file>      Output report file\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string instance_file = argv[1];
    std::string method_name = argv[2];
    std::string output_file;

    RunConfig config;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--M" && i + 1 < argc) {
            config.M = std::stod(argv[++i]);
        } else if (arg == "--kappa" && i + 1 < argc) {
            config.kappa = std::stod(argv[++i]);
        } else if (arg == "--tol" && i + 1 < argc) {
            config.pricing_tol = std::stod(argv[++i]);
        } else if (arg == "--max-rounds" && i + 1 < argc) {
            config.max_rounds = std::stoi(argv[++i]);
        } else if (arg == "--extra-rounds" && i + 1 < argc) {
            config.max_extra_rounds = std::stoi(argv[++i]);
        } else if (arg == "--time-limit" && i + 1 < argc) {
            config.time_limit = std::chrono::seconds(std::stoi(argv[++i]));
        } else if (arg == "--lp-time-limit" && i + 1 < argc) {
            config.lp_time_limit = std::chrono::seconds(std::stoi(argv[++i]));
        } else if (arg == "--price-all") {
            config.price_all_negative = true;
        } else if (arg == "--use-initial-columns") {
            config.use_initial_columns = true;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    try {
        Instance instance = Instance::load(instance_file);

        auto method = create_method(method_name, instance, config);

        Stats& stats = const_cast<Stats&>(method->stats());
        stats.set_method_name(method_name);
        stats.set_instance_name(instance.name());
        stats.set_config(config);

        InitMethodResult result = method->Run();
        stats.set_termination(result);

        RunReport report(stats);

        std::string default_output = "results/" + instance.name() + "." + method_name + ".json";
        std::string final_output = output_file.empty() ? default_output : output_file;
        report.write_file(final_output);

        std::cout << "Instance: " << instance.name() << "\n";
        std::cout << "Method: " << method_name << "\n";
        std::cout << "Status: " << [&]() {
            switch (result.status) {
                case TerminationStatus::kFeasible: return "feasible";
                case TerminationStatus::kCertifiedInfeasible: return "certified_infeasible";
                case TerminationStatus::kBudgetExhausted: return "budget_exhausted";
                default: return "error";
            }
        }() << "\n";
        std::cout << "Iterations: " << result.iterations_to_endpoint << "\n";
        std::cout << "Final objective: " << result.final_objective << "\n";
        if (result.reference_objective > 0) {
            std::cout << "Reference objective: " << result.reference_objective << "\n";
            Real gap = (result.final_objective - result.reference_objective) / std::abs(result.reference_objective);
            std::cout << "Gap: " << gap * 100 << "%\n";
        }
        std::cout << "Report written to: " << final_output << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}