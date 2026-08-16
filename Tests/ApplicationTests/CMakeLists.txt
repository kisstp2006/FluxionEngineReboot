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

engine_add_test(
    NAME ApplicationTests
    SOURCES
        main.c
        Test_EventQueue.c
        Test_Window.c
        Test_WindowEvents.c
        Test_Display.c
        Test_Clipboard.c
        Test_Input_Keyboard.c
        Test_Input_Mouse.c
        Test_Input_Gamepad.c
        Test_InputAction.c
        Test_Time.c
    DEPENDENCIES
        Fluxion::Application
        Fluxion::Foundation
        # Test_Time.c sleeps between frames to make the clock advance, and
        # the sleep is the platform's -- Application itself never exposes
        # one.
        Fluxion::Platform
)

if(UNIX)
    # Test_WindowEvents.c talks to X11 directly (to inject a native close
    # message and prove the translation into FluxionEvent), so the test
    # target needs the same X11 access as the Application module itself.
    find_package(X11 REQUIRED)
    target_include_directories(ApplicationTests PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(ApplicationTests PRIVATE ${X11_LIBRARIES})
endif()

engine_add_header_selfcontained_test(
    MODULE Application
    HEADERS
        Fluxion/Application/Events/Event.h
        Fluxion/Application/Events/EventQueue.h
        Fluxion/Application/Input/Gamepad.h
        Fluxion/Application/Input/Input.h
        Fluxion/Application/Input/InputAction.h
        Fluxion/Application/Input/KeyCode.h
        Fluxion/Application/Input/MouseButton.h
        Fluxion/Application/Time/Time.h
        Fluxion/Application/Window/Clipboard.h
        Fluxion/Application/Window/Display.h
        Fluxion/Application/Window/Window.h
        Fluxion/Application/Window/WindowHandle.h
)
