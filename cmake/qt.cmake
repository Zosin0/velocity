# Qt6 integration configuration for portable Qt.
# Sets up CMAKE_PREFIX_PATH and provides a deployment function.

set(Qt6_DIR "${CMAKE_SOURCE_DIR}/external/qt6/6.8.0/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 directory path")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/qt6/6.8.0/msvc2022_64")

# Enable automatic MOC (Meta-Object Compiler)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Runs windeployqt on the output executable to copy runtime Qt DLLs and plugins.
function(velocity_copy_qt_dlls target)
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "EXECUTABLE")
        return()
    endif()

    set(windeployqt_exe "${CMAKE_SOURCE_DIR}/external/qt6/6.8.0/msvc2022_64/bin/windeployqt.exe")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${windeployqt_exe}" --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler "$<TARGET_FILE:${target}>"
        COMMENT "Running windeployqt for ${target}"
    )
endfunction()
