add_executable(velocity
    main.cpp
)
target_link_libraries(velocity PRIVATE
    velocity_options
    velocity::foundation
    velocity::media
)
velocity_copy_ffmpeg_dlls(velocity)
