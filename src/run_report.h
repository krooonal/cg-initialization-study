#pragma once

#include "stats.h"
#include "json.h"
#include <string>
#include <chrono>

namespace cg {

class RunReport {
public:
    explicit RunReport(const Stats& stats);

    JsonValue to_json() const;
    void write_file(const std::string& filepath) const;

private:
    const Stats& stats_;
};

} // namespace cg