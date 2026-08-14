# Fluxion Engine

Open source version of the Fluxion Engine.

Written in C and C++ (C23 / C++23).

## Why open source

The original, closed-source Fluxion had grown too large to keep patching,
refactoring, or restructuring in place. Rather than fight that codebase, it
is being rewritten from scratch, carrying over what still makes sense from
the original (everything written by me), and released under a new,
permissive license that puts as few restrictions as possible on who can use
the engine and how.

Unlike the original, which was written mostly in C#, this version is
written in C and C++ — to natively support more platforms with as few
wrappers as possible, to support at least the scenes of the original
Fluxion, and to be faster than the current C#/C++ version. Its architecture
and extensibility model draw ideas from established AAA engines.

## Building

Every target is a CMake preset — `cmake --list-presets` shows the ones your
host can run.

| Preset | Builds for | Needs |
|---|---|---|
| `windows-msvc` | Windows x64 | Visual Studio, the Vulkan SDK |
| `linux-gcc`, `linux-gcc-release` | Linux x64 | GCC, Ninja, the Vulkan SDK, `libx11-dev` and `libxrandr-dev` |
| `linux-aarch64` | 64-bit ARM Linux, cross-compiled | `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`, and the target's own libraries under the sysroot |
| `android-arm64` | Android ARM64, cross-compiled | the Android NDK, with `ANDROID_NDK_ROOT` naming it |

The two cross targets build what exists for them and stop, with a message
naming what does not: the window, display, input and clipboard backend for
Android has not been written, so anything above it — the renderer and the
scene — cannot be built there yet. Everything below it can, and is.

Their tests are compiled but not registered with CTest, because a binary for
another machine cannot be started on this one. Compiling them is still worth
doing on its own: it is what checks that the engine's headers and that
toolchain agree.

## Third party

| Library | Used for | License |
|---|---|---|
| [pdjson](https://github.com/skeeto/pdjson) | Parsing `.plugin` descriptor files | Unlicense (Public Domain) |
| [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory sub-allocation in the Vulkan RHI backend | MIT |
| [D3D12MemoryAllocator (D3D12MA)](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | GPU memory sub-allocation in the D3D12 RHI backend | MIT |
| The Khronos Group `glcorearb.h` / `khrplatform.h` / `wglext.h` / `glxext.h` | GL 1.2+ and WGL/GLX extension declarations in the OpenGL RHI backend | Khronos Free Use License |
