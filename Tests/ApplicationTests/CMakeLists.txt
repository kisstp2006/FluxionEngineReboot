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
    DEPENDENCIES
        Fluxion::Application
        Fluxion::Foundation
)

if(UNIX)
    # Test_WindowEvents.c talks to X11 directly (to inject a native close
    # message and prove the translation into FluxionEvent), so the test
    # target needs the same X11 access as the Application module itself.
    find_package(X11 REQUIRED)
    target_include_directories(ApplicationTests PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(ApplicationTests PRIVATE ${X11_LIBRARIES})
endif()
