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

# The SPDX tag, not a sentence out of the notice.
#
# The notice is wrapped across lines, so no phrase in it long enough to be
# unmistakable survives as one contiguous string -- which is exactly how
# the first version of this check managed to report every file in the
# engine as missing a notice it plainly had. The tag is one line, one
# token, and is meant to be read by machines.
set(FLUXION_LICENSE_MARKER "SPDX-License-Identifier: CPAL-1.0")

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
