engine_add_test(
    NAME SceneTests
    SOURCES
        main.cpp
        Test_Hierarchy.cpp
        Test_Transform.cpp
        Test_Components.cpp
        Test_Attributes.cpp
        Test_EngineApi.cpp
        Test_Reload.cpp
        Test_DataComponents.cpp
        Test_Archetype.cpp
        Test_TransformUpdate.cpp
        Test_Systems.cpp
        Test_ScriptReflection.cpp
        Test_SceneSerialization.cpp
        Test_EntityUUID.cpp
        Test_CommandBuffer.cpp
        Test_World.cpp
        Test_SceneAssetReferences.cpp
        Test_SceneLights.cpp
    DEPENDENCIES
        Fluxion::Scene
        Fluxion::Script
        # Test_EngineApi.cpp drives the clock and feeds the input state
        # directly, so it sees what a component running on top of them
        # sees.
        Fluxion::Application
        # The asset reference checks name the field type an asset
        # reference is recognised by.
        Fluxion::Assets
    NO_EXCEPTIONS
    NO_RTTI
)

engine_add_header_selfcontained_test(
    MODULE Scene
    HEADERS
        Fluxion/Scene/Scene.h
        Fluxion/Scene/EntityCommandBuffer.h
        Fluxion/Scene/EntityQuery.h
        Fluxion/Scene/Transform.h
        Fluxion/Scene/SystemScheduler.h
        Fluxion/Scene/ScriptReflection.h
        Fluxion/Scene/SceneSerialization.h
        Fluxion/Scene/Prefab.h
        Fluxion/Scene/World.hpp
        Fluxion/Scene/SceneScript.hpp
        Fluxion/Scene/EngineScript.hpp
)
