engine_add_module(
    NAME Foundation
    TYPE STATIC
    LANGUAGE C CXX
)

if(UNIX)
    # Math.h (sqrtf, ...) is header-only, so this cost is invisible until a
    # consumer actually links — attach it PUBLIC here so every module that
    # links Fluxion::Foundation gets it transitively. Windows doesn't need
    # this: the math functions are already part of the CRT.
    target_link_libraries(Foundation PUBLIC m)
endif()
