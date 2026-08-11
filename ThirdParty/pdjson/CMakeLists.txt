# pdjson (https://github.com/skeeto/pdjson) — vendored, unmodified, Unlicense.
# Third-party code: not run through engine_set_warnings or forced onto our
# C23 setting; it's written for plain C99 and we leave it that way.
add_library(pdjson STATIC pdjson.c)
target_include_directories(pdjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
