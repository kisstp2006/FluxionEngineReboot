include_guard(GLOBAL)

# Warnings are errors, because a warning nobody has to act on is a
# warning nobody reads: once there are a handful of tolerated ones, a new
# one arrives into a list already being scrolled past. The tree builds
# clean on both toolchains, so this costs nothing to hold to.
#
# It is deliberately a default rather than a rule. A compiler upgrade can
# introduce a warning that has nothing to do with any change being made,
# and having to fix that before anything can build is not a good trade
# for someone mid-task -- FLUXION_WARNINGS_AS_ERRORS=OFF turns it off for
# that build. CI does not pass it, so nothing merges on the strength of
# having turned it off locally.
option(FLUXION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# engine_set_warnings(<target>)
#
# Applies the project's standard warning level to a target.
function(engine_set_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
        if(FLUXION_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        if(FLUXION_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()
    endif()
endfunction()
