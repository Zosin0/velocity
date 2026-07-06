#pragma once
#include <spdlog/spdlog.h>

namespace velocity::log {

// Initializes the global logger (colored stderr sink). Idempotent.
void init(const char* appName);

} // namespace velocity::log
