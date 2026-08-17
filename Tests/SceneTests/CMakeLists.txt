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
        Test_SceneCamera.cpp
        Test_RenderExtraction.cpp
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
