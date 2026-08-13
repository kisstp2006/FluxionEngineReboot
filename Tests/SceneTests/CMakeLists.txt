engine_add_test(
    NAME SceneTests
    SOURCES
        main.cpp
        Test_Hierarchy.cpp
        Test_Transform.cpp
        Test_Components.cpp
        Test_Attributes.cpp
    DEPENDENCIES
        Fluxion::Scene
        Fluxion::Script
    NO_EXCEPTIONS
    NO_RTTI
)

engine_add_header_selfcontained_test(
    MODULE Scene
    HEADERS
        Fluxion/Scene/Scene.h
        Fluxion/Scene/SceneScript.hpp
)
