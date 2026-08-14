engine_add_module(
    NAME Foundation
    TYPE STATIC
    LANGUAGE C CXX
    NO_EXCEPTIONS
    NO_RTTI
)

if(UNIX)
    # Math.h (sqrtf, ...) is header-only, so this cost is invisible until a
    # consumer actually links — attach it PUBLIC here so every module that
    # links Fluxion::Foundation gets it transitively. Windows doesn't need
    # this: the math functions are already part of the CRT.
    target_link_libraries(Foundation PUBLIC m)

    # CMAKE_C_EXTENSIONS is OFF project-wide, so this compiles as strict
    # ISO C, which hides everything POSIX behind a feature-test macro --
    # including posix_memalign, which the aligned allocator needs, and
    # clock_gettime, which the id generator seeds from. Same macro and
    # same reason as the Platform module's own Linux backend.
    target_compile_definitions(Foundation PRIVATE _GNU_SOURCE)
endif()
