add_executable(InputDemo main.c)
target_link_libraries(InputDemo PRIVATE Fluxion::Application Fluxion::Platform Fluxion::Foundation)
engine_set_warnings(InputDemo)
