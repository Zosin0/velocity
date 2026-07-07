# --- Generated test media -----------------------------------------------------
# Fixtures are generated at build time with the bundled ffmpeg.exe. This is a
# TEST FIXTURE GENERATOR only — product code never shells out to ffmpeg.exe
# (docs/04). libopenh264 is used so generation is deterministic on any machine.
set(VELOCITY_TESTMEDIA_DIR ${CMAKE_BINARY_DIR}/testmedia)
file(MAKE_DIRECTORY ${VELOCITY_TESTMEDIA_DIR})

set(_ffmpeg_exe ${VELOCITY_FFMPEG_BIN}/ffmpeg.exe)

add_custom_command(
    OUTPUT ${VELOCITY_TESTMEDIA_DIR}/counting_30fps.mp4
    COMMAND ${_ffmpeg_exe} -y -loglevel error -f lavfi -i testsrc2=size=320x240:rate=30
            -t 2 -c:v libopenh264 -pix_fmt yuv420p ${VELOCITY_TESTMEDIA_DIR}/counting_30fps.mp4
    COMMENT "Generating counting_30fps.mp4 (60 frames)")

add_custom_command(
    OUTPUT ${VELOCITY_TESTMEDIA_DIR}/counting_ntsc.mp4
    COMMAND ${_ffmpeg_exe} -y -loglevel error -f lavfi -i testsrc2=size=320x240:rate=30000/1001
            -t 4 -c:v libopenh264 -pix_fmt yuv420p ${VELOCITY_TESTMEDIA_DIR}/counting_ntsc.mp4
    COMMENT "Generating counting_ntsc.mp4 (120 frames @ 29.97)")

add_custom_command(
    OUTPUT ${VELOCITY_TESTMEDIA_DIR}/counting_av.mp4
    COMMAND ${_ffmpeg_exe} -y -loglevel error
            -f lavfi -i testsrc2=size=320x240:rate=30
            -f lavfi -i sine=frequency=440:sample_rate=48000
            -t 2 -c:v libopenh264 -pix_fmt yuv420p -c:a aac -ac 2 -shortest
            ${VELOCITY_TESTMEDIA_DIR}/counting_av.mp4
    COMMENT "Generating counting_av.mp4 (60 frames + 440Hz tone)")

add_custom_target(velocity_testmedia DEPENDS
    ${VELOCITY_TESTMEDIA_DIR}/counting_30fps.mp4
    ${VELOCITY_TESTMEDIA_DIR}/counting_ntsc.mp4
    ${VELOCITY_TESTMEDIA_DIR}/counting_av.mp4)

# --- Unit tests ----------------------------------------------------------------
add_executable(velocity_unit_tests
    time_tests.cpp
    media_tests.cpp
    decoder_tests.cpp
    gpu_tests.cpp
    audio_tests.cpp
    model_tests.cpp
    linked_edit_tests.cpp
    compositor_tests.cpp
    export_tests.cpp
    audio_mix_tests.cpp
    project_io_tests.cpp
)
target_link_libraries(velocity_unit_tests PRIVATE
    velocity_options
    velocity::foundation
    velocity::media
    velocity::gpu
    velocity::audio
    velocity::engine
    GTest::gtest_main
)
add_dependencies(velocity_unit_tests velocity_testmedia)
velocity_copy_ffmpeg_dlls(velocity_unit_tests)

include(GoogleTest)
gtest_discover_tests(velocity_unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "VELOCITY_TESTMEDIA_DIR=${VELOCITY_TESTMEDIA_DIR}")
