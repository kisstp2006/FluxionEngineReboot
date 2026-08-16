# Every source file the engine owns has to carry the license notice.
#
# A rule nobody checks is a rule that lasts until the next busy afternoon.
# The notice is what the license this engine is under actually asks for --
# see Exhibit A in license.md -- and a file that quietly went out without
# it is not something anyone would notice by reading the diff of the change
# that added it.
#
# Run as a test, not as a build step: a missing notice is not a reason to
# refuse to compile, but it is a reason to fail.

get_filename_component(FLUXION_LICENSE_CHECK_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# The SPDX tag, not a sentence out of the notice: the notice is wrapped
# across lines, so no phrase in it survives as one contiguous string --
# which is how the first version of this check reported every file as
# missing a notice it plainly had. The tag is one line, one token.
set(FLUXION_LICENSE_MARKER "SPDX-License-Identifier: CPAL-1.0")

# The fast path: one grep process instead of hundreds of file(READ)s.
# Measured at twenty times quicker -- CMake's own reads carry enough
# overhead that they, not the disk, are the cost. The CMake loop below
# stays as the fallback for a machine with no grep, and both answer the
# same question over the same files.
find_program(FLUXION_LICENSE_GREP grep)
if(FLUXION_LICENSE_GREP)
    execute_process(
        COMMAND "${FLUXION_LICENSE_GREP}" -r -L
                --include=*.c --include=*.cpp --include=*.h --include=*.hpp
                --include=*.jsl --include=*.fls
                --include=CMakeLists.txt --include=*.cmake
                "${FLUXION_LICENSE_MARKER}"
                Source Tests Samples CMake
        WORKING_DIRECTORY "${FLUXION_LICENSE_CHECK_ROOT}"
        OUTPUT_VARIABLE FLUXION_LICENSE_GREP_MISSING
        RESULT_VARIABLE FLUXION_LICENSE_GREP_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    # grep answers 0 or 1 depending on whether anything matched; both are
    # valid runs. Anything else is grep itself failing, and then the slow
    # path below answers instead.
    if(FLUXION_LICENSE_GREP_RESULT LESS 2)
        if(FLUXION_LICENSE_GREP_MISSING STREQUAL "")
            message("All engine source files carry the license notice.")
            return()
        endif()
        string(REPLACE "\n" ";" FLUXION_LICENSE_GREP_LIST "${FLUXION_LICENSE_GREP_MISSING}")
        list(LENGTH FLUXION_LICENSE_GREP_LIST FLUXION_LICENSE_GREP_COUNT)
        message("")
        message("${FLUXION_LICENSE_GREP_COUNT} file(s) carry no license notice:")
        foreach(FLUXION_LICENSE_GREP_FILE IN LISTS FLUXION_LICENSE_GREP_LIST)
            message("    ${FLUXION_LICENSE_GREP_FILE}")
        endforeach()
        message("")
        message("Copy the block from the top of any neighbouring file, or the text of")
        message("Exhibit A in license.md. It ends with the ${FLUXION_LICENSE_MARKER} line.")
        message(FATAL_ERROR "license notices are missing")
    endif()
endif()

# ThirdParty is deliberately absent: what is under there belongs to other
# people and carries their notices, and a CPAL header inside that folder
# would say something untrue about the code beside it.
file(GLOB_RECURSE FLUXION_LICENSE_CHECK_FILES
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/*.c"
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/*.cpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/*.h"
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/*.hpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/*.jsl"
     "${FLUXION_LICENSE_CHECK_ROOT}/Source/CMakeLists.txt"
     "${FLUXION_LICENSE_CHECK_ROOT}/Tests/*.c"
     "${FLUXION_LICENSE_CHECK_ROOT}/Tests/*.cpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Tests/*.h"
     "${FLUXION_LICENSE_CHECK_ROOT}/Tests/*.hpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Tests/CMakeLists.txt"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.c"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.cpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.h"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.hpp"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.jsl"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/*.fls"
     "${FLUXION_LICENSE_CHECK_ROOT}/Samples/CMakeLists.txt"
     "${FLUXION_LICENSE_CHECK_ROOT}/CMake/*.cmake")

set(FLUXION_LICENSE_MISSING "")
set(FLUXION_LICENSE_CHECKED 0)

foreach(candidate IN LISTS FLUXION_LICENSE_CHECK_FILES)
    # A build tree inside the checkout would otherwise drag in every
    # generated and copied file underneath it.
    if(candidate MATCHES "/build/")
        continue()
    endif()

    math(EXPR FLUXION_LICENSE_CHECKED "${FLUXION_LICENSE_CHECKED} + 1")

    # The head of the file only. The notice belongs at the top, and reading
    # whole files here would mean reading the entire engine to answer a
    # question about its first forty lines.
    file(READ "${candidate}" head LIMIT 3000)

    string(FIND "${head}" "${FLUXION_LICENSE_MARKER}" found)
    if(found EQUAL -1)
        file(RELATIVE_PATH shown "${FLUXION_LICENSE_CHECK_ROOT}" "${candidate}")
        list(APPEND FLUXION_LICENSE_MISSING "${shown}")
    endif()
endforeach()

list(LENGTH FLUXION_LICENSE_MISSING missing_count)

if(missing_count GREATER 0)
    message("")
    message("${missing_count} of ${FLUXION_LICENSE_CHECKED} file(s) carry no license notice:")
    foreach(shown IN LISTS FLUXION_LICENSE_MISSING)
        message("    ${shown}")
    endforeach()
    message("")
    message("Copy the block from the top of any neighbouring file, or the text of")
    message("Exhibit A in license.md, and put it above everything else in the file.")
    message("It ends with the ${FLUXION_LICENSE_MARKER} line, which is what is looked for.")
    message("")
    message(FATAL_ERROR "license notices are missing")
endif()

message("All ${FLUXION_LICENSE_CHECKED} engine source file(s) carry the license notice.")
