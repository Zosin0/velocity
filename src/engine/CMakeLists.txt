add_library(velocity_engine STATIC
    src/edits.cpp
    src/compile.cpp
    src/export.cpp
)
target_include_directories(velocity_engine PUBLIC include)
target_link_libraries(velocity_engine
    PUBLIC velocity::foundation velocity::media
    PRIVATE velocity_options
)
add_library(velocity::engine ALIAS velocity_engine)
