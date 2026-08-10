add_library(PlatformTestPlugin SHARED TestPlugin.c)
target_link_libraries(PlatformTestPlugin PRIVATE Fluxion::Foundation)
