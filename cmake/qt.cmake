# Qt6 integration configuration.
# Locally, the portable Qt in external/qt6 is used; in CI (or any machine with
# Qt elsewhere) CMAKE_PREFIX_PATH / Qt6_DIR select the install. windeployqt is
# always resolved from the Qt actually found, never from a hardcoded path.

set(_velocity_portable_qt "${CMAKE_SOURCE_DIR}/external/qt6/6.8.0/msvc2022_64")
if(EXISTS "${_velocity_portable_qt}/lib/cmake/Qt6")
    list(APPEND CMAKE_PREFIX_PATH "${_velocity_portable_qt}")
endif()

# Enable automatic MOC (Meta-Object Compiler)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets Concurrent Svg REQUIRED)

# Resolve windeployqt next to the found Qt's binaries (Qt6::qmake lives in
# <prefix>/bin for every official layout, portable or aqt-installed).
if(TARGET Qt6::qmake)
    get_target_property(_velocity_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_velocity_qt_bin "${_velocity_qmake}" DIRECTORY)
else()
    get_filename_component(_velocity_qt_bin "${Qt6_DIR}/../../../bin" ABSOLUTE)
endif()
find_program(VELOCITY_WINDEPLOYQT windeployqt HINTS "${_velocity_qt_bin}" REQUIRED)

# Runs windeployqt on the output executable to copy runtime Qt DLLs and plugins.
function(velocity_copy_qt_dlls target)
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "EXECUTABLE")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${VELOCITY_WINDEPLOYQT}" --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler "$<TARGET_FILE:${target}>"
        COMMENT "Running windeployqt for ${target}"
    )
endfunction()
