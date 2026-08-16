# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

# Private/Events/*.c, Private/Input/*.c and Private/Time/*.c are
# platform-independent (Foundation-backed logic over a counter Platform
# already offers everywhere) and always compiled in, alongside whichever
# OS backend applies.
file(GLOB APPLICATION_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Events/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Input/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Time/*.c"
)

# A window, a display, a clipboard and an input device all come from the
# OS, so this module cannot stand without a backend either.
engine_platform_sources(APPLICATION_PLATFORM_SOURCES Application REQUIRED)

set(APPLICATION_SOURCES ${APPLICATION_COMMON_SOURCES} ${APPLICATION_PLATFORM_SOURCES})

engine_add_module(
    NAME Application
    TYPE STATIC
    LANGUAGE C CXX
    SOURCES ${APPLICATION_SOURCES}
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
    PRIVATE_DEPENDENCIES
        # Private/Time/Time.c only -- the counter it builds on is the
        # platform's, but no public header of this module exposes it, so
        # nothing that links Application has to know Platform exists.
        Fluxion::Platform
)

if(WIN32)
    # GetDpiForMonitor (Display_Windows.c) lives in Shcore, and
    # XInputGetState (Input_Windows.c) in Xinput — neither is part of the
    # default link set.
    target_link_libraries(Application PRIVATE Shcore Xinput)
elseif(FLUXION_PLATFORM_NAME STREQUAL "Linux")
    # Asked for by name rather than by "is this UNIX": Android is UNIX too
    # and has no X server, so an X11 search there would either fail or --
    # worse, on a host with the headers lying around -- succeed against
    # libraries that will not exist on the device.
    find_package(X11 REQUIRED)
    if(NOT X11_Xrandr_FOUND)
        message(FATAL_ERROR "Fluxion Application module needs the Xrandr X11 extension (libxrandr-dev)")
    endif()
    target_include_directories(Application PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(Application PRIVATE ${X11_LIBRARIES} ${X11_Xrandr_LIB})
endif()
