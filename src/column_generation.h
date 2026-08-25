#pragma once

#include "init_method.h"
#include "rmp_model.h"
#include "shortest_path.h"
#include "stats.h"
#include "instance.h"
#include <memory>

namespace cg {

std::unique_ptr<InitMethod> create_method(const std::string& method_name,
                                          const Instance& instance,
                                          const RunConfig& config);

} // namespace cg