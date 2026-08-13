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
