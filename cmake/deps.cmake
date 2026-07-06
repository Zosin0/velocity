# Third-party dependencies via FetchContent, pinned to exact release tags.
# NOTE: docs/01 specifies vcpkg manifest mode as the long-term dependency
# manager. FetchContent is the bootstrap-phase substitute (no vcpkg on this
# machine, and building FFmpeg from source is out of session budget).
# Migration to vcpkg is tracked in docs/PROGRESS.md.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
    GIT_SHALLOW ON
    SYSTEM
)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW ON
    SYSTEM
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW ON
    SYSTEM
)

FetchContent_MakeAvailable(spdlog nlohmann_json googletest)
