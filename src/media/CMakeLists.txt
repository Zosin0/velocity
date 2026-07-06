add_library(velocity_media STATIC
    src/ffmpeg_info.cpp
    src/probe.cpp
    src/video_decoder.cpp
)
target_include_directories(velocity_media PUBLIC include)
target_link_libraries(velocity_media
    PUBLIC velocity::foundation
    PRIVATE velocity_options
            ffmpeg::avformat ffmpeg::avcodec ffmpeg::avutil
            ffmpeg::swscale ffmpeg::swresample
)
add_library(velocity::media ALIAS velocity_media)
