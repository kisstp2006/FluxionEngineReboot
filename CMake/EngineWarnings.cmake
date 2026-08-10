include_guard(GLOBAL)

# engine_set_warnings(<target>)
#
# Applies the project's standard warning level to a target. Not treated as
# errors by default (early bootstrap milestones would otherwise get blocked
# on minor issues) — revisit once the codebase is bigger.
function(engine_set_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endfunction()
