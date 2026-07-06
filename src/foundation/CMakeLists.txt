add_library(velocity_foundation STATIC
    src/log.cpp
)
target_include_directories(velocity_foundation PUBLIC include)
target_link_libraries(velocity_foundation
    PUBLIC spdlog::spdlog
    PRIVATE velocity_options
)
add_library(velocity::foundation ALIAS velocity_foundation)
