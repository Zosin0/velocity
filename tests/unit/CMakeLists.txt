add_executable(velocity_unit_tests
    time_tests.cpp
    media_tests.cpp
)
target_link_libraries(velocity_unit_tests PRIVATE
    velocity_options
    velocity::foundation
    velocity::media
    GTest::gtest_main
)
velocity_copy_ffmpeg_dlls(velocity_unit_tests)

include(GoogleTest)
gtest_discover_tests(velocity_unit_tests DISCOVERY_MODE PRE_TEST)
