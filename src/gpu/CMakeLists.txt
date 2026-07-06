add_library(velocity_gpu STATIC
    src/device.cpp
    src/swapchain.cpp
    src/window.cpp
)
target_include_directories(velocity_gpu PUBLIC include)
target_link_libraries(velocity_gpu
    PUBLIC velocity::foundation
           d3d12 dxgi dxguid user32
    PRIVATE velocity_options
)
add_library(velocity::gpu ALIAS velocity_gpu)
