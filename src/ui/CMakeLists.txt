# CMake file for the Qt UI library
add_library(velocity_ui STATIC
    shell/mainwindow.h
    shell/mainwindow.cpp
    shell/theming.h
    shell/theming.cpp
    shell/documentsession.h
    shell/documentsession.cpp
    preview/previewwidget.h
    preview/previewwidget.cpp
    timeline/timeline_widget.h
    timeline/timeline_widget.cpp
    inspector/inspector_widget.h
    inspector/inspector_widget.cpp
    bin/media_bin_widget.h
    bin/media_bin_widget.cpp
    mixer/mixer_widget.h
    mixer/mixer_widget.cpp
    exportdlg/export_dialog.h
    exportdlg/export_dialog.cpp
    playback/playback_controller.h
    playback/playback_controller.cpp
    services/waveform_cache.h
    services/waveform_cache.cpp
    services/thumbnail_cache.h
    services/thumbnail_cache.cpp
    services/frame_conversion.h
    services/frame_conversion.cpp
    shell/icons.h
    shell/icons.cpp
)

target_include_directories(velocity_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(velocity_ui PUBLIC
    velocity_options
    velocity::foundation
    velocity::gpu
    velocity::engine
    velocity::media
    velocity::audio
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
)
