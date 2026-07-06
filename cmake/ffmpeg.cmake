# Imported targets for the prebuilt FFmpeg shared libraries downloaded by
# tools/setup-devenv.ps1. Root is external/ffmpeg, overridable via the
# VELOCITY_FFMPEG_ROOT environment variable (set by tools/devshell.ps1).

set(_ff_root "$ENV{VELOCITY_FFMPEG_ROOT}")
if(NOT _ff_root)
    set(_ff_root "${CMAKE_SOURCE_DIR}/external/ffmpeg")
endif()
cmake_path(NORMAL_PATH _ff_root)

if(NOT EXISTS "${_ff_root}/include/libavformat/avformat.h")
    message(FATAL_ERROR "FFmpeg not found at '${_ff_root}'. Run tools/setup-devenv.ps1 first.")
endif()

set(VELOCITY_FFMPEG_BIN "${_ff_root}/bin" CACHE INTERNAL "FFmpeg DLL directory")

function(_velocity_ffmpeg_lib name)
    add_library(ffmpeg::${name} SHARED IMPORTED GLOBAL)
    file(GLOB _dll "${_ff_root}/bin/${name}-*.dll")
    set_target_properties(ffmpeg::${name} PROPERTIES
        IMPORTED_LOCATION "${_dll}"
        IMPORTED_IMPLIB "${_ff_root}/lib/${name}.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_ff_root}/include"
    )
endfunction()

foreach(_lib avutil avcodec avformat avfilter swscale swresample)
    _velocity_ffmpeg_lib(${_lib})
endforeach()

# Copies the FFmpeg runtime DLLs next to a target's output binary.
function(velocity_copy_ffmpeg_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${VELOCITY_FFMPEG_BIN}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying FFmpeg DLLs for ${target}")
endfunction()
