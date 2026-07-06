cmake_minimum_required(VERSION 3.28)
project(Velocity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "Velocity targets Windows only.")
endif()

# Strict flags for our code; third-party targets are excluded (they are built
# via FetchContent with their own settings and warnings suppressed as SYSTEM).
add_library(velocity_options INTERFACE)
target_compile_options(velocity_options INTERFACE
    /W4 /WX /permissive- /EHsc /utf-8 /Zc:preprocessor
    $<$<CONFIG:Release>:/O2 /GS>
)
target_compile_definitions(velocity_options INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)

include(cmake/deps.cmake)
include(cmake/ffmpeg.cmake)

enable_testing()

add_subdirectory(src/foundation)
add_subdirectory(src/media)
add_subdirectory(src/main)
add_subdirectory(tests/unit)
