add_library(velocity_audio STATIC
    src/output.cpp
)
target_include_directories(velocity_audio PUBLIC include)
target_link_libraries(velocity_audio
    PUBLIC velocity::foundation
    PRIVATE velocity_options ole32 avrt
)
add_library(velocity::audio ALIAS velocity_audio)
