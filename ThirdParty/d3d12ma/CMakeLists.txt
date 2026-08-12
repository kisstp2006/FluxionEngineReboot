# D3D12 Memory Allocator (https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
# — vendored, unmodified, MIT (see LICENSE.txt). Third-party code: not run
# through engine_set_warnings or forced onto our C23/C++23 setting.

add_library(d3d12ma STATIC D3D12MemAlloc.cpp)
target_include_directories(d3d12ma PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(d3d12ma PUBLIC d3d12 dxgi)
