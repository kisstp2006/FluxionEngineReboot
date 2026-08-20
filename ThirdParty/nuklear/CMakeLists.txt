# Nuklear (https://github.com/Immediate-Mode-UI/Nuklear) — vendored,
# unmodified, dual licensed public domain / MIT.
#
# Third-party code: not run through engine_set_warnings and not forced onto
# our C23 setting; it is written for C89 and we leave it that way.
add_library(nuklear STATIC nuklear_impl.c)
target_include_directories(nuklear PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The same switches the implementation is built with, given to everyone who
# includes the header -- a caller that saw a different set would see a
# different struct layout, which links cleanly and then reads the wrong
# fields.
target_compile_definitions(nuklear PUBLIC
    NK_INCLUDE_FIXED_TYPES
    NK_INCLUDE_STANDARD_VARARGS
    NK_INCLUDE_FONT_BAKING
    NK_INCLUDE_DEFAULT_FONT
    NK_INCLUDE_VERTEX_BUFFER_OUTPUT)

if(MSVC)
    # It is C89 with unions and casts throughout; our warnings-as-errors
    # are about our code.
    target_compile_options(nuklear PRIVATE /W0)
else()
    target_compile_options(nuklear PRIVATE -w)
endif()
