#include "column_generation.h"
#include "methods/big_m.h"
#include "methods/two_phase.h"
#include "methods/farkas.h"
#include <stdexcept>

namespace cg {

std::unique_ptr<InitMethod> create_method(const std::string& method_name,
                                          const Instance& instance,
                                          const RunConfig& config) {
    if (method_name == "big_m") {
        auto method = std::make_unique<BigMMethod>(instance, config);
        return method;
    } else if (method_name == "two_phase") {
        auto method = std::make_unique<TwoPhaseMethod>(instance, config);
        return method;
    } else if (method_name == "farkas") {
        auto method = std::make_unique<FarkasMethod>(instance, config);
        return method;
    }
    throw std::invalid_argument("Unknown method: " + method_name);
}

} // namespace cg