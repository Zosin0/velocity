#include "velocity/foundation/log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>

namespace velocity::log {

void init(const char* appName) {
    static std::once_flag once;
    std::call_once(once, [appName] {
        auto logger = spdlog::stdout_color_mt(appName);
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
#ifndef NDEBUG
        spdlog::set_level(spdlog::level::debug);
#endif
    });
}

} // namespace velocity::log
