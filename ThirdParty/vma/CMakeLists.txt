# VulkanMemoryAllocator (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
# — vendored, unmodified, MIT (see LICENSE.txt). Third-party code: not run
# through engine_set_warnings or forced onto our C23/C++23 setting.
find_package(Vulkan REQUIRED)

add_library(vma STATIC vma_impl.cpp)
target_include_directories(vma PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vma PUBLIC Vulkan::Vulkan)
