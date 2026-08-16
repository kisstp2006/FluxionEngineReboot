// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

// Collecting a scene's lights into the flat list a renderer wants.
//
// The scene holds a light as a component on an object whose world matrix
// the transform update has just finished working out. A renderer wants
// world-space positions and directions in one array. This is where the
// one becomes the other -- and it is in this module rather than in
// RenderCore because RenderCore does not know what a scene is, and this
// change is not the moment to teach it.

#include "SceneInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Light.h>
#include <Fluxion/Scene/SceneSerialization.h>

#include <math.h>
#include <string.h>

// Which way an object faces.
//
// Negative Z, matching the camera: a view looking down its own negative
// Z is what every projection built in this engine assumes, and a light
// that used a different axis would point somewhere a person aiming it by
// eye did not expect.
//
// Taken from the world matrix rather than the local rotation, so a light
// parented to something turning turns with it.
static FluxionVec3 Fluxion_SceneLight_Forward(const FluxionMat4* world)
{
    FluxionVec3 forward;
    forward.x = -world->m[0][2];
    forward.y = -world->m[1][2];
    forward.z = -world->m[2][2];
    return Fluxion_Vec3_Normalize(forward);
}

static FluxionVec3 Fluxion_SceneLight_Position(const FluxionMat4* world)
{
    FluxionVec3 position;
    position.x = world->m[0][3];
    position.y = world->m[1][3];
    position.z = world->m[2][3];
    return position;
}

// One kind of light, over every block that carries it.
//
// `written` is both read and written: the three kinds share one output
// array, and each pass over the scene continues where the last left off.
static void Fluxion_SceneLight_GatherOne(FluxionSceneHandle scene, FluxionTypeId lightType,
                                         FluxionRenderLight* out, u32 capacity, u32* written, u32* total)
{
    const FluxionTypeId required[2] = { lightType, Fluxion_Transform_TypeId() };

    FluxionEntityQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.required = required;
    desc.requiredCount = 2;

    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;

    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        const FluxionTransform* transforms = (const FluxionTransform*)Fluxion_EntityChunk_Column(&chunk, Fluxion_Transform_TypeId());
        const void* lights = Fluxion_EntityChunk_Column(&chunk, lightType);
        if (transforms == NULL || lights == NULL) continue;

        for (u32 i = 0; i < chunk.count; ++i)
        {
            // Counted whether or not there is room to write it. The count
            // is what sizes the next call, and a count that stopped at
            // the buffer's end would make asking twice give two different
            // answers -- which is exactly the bug this same shape had in
            // the asset gather, found by its own test.
            ++(*total);
            if (out == NULL || *written >= capacity) continue;

            const FluxionMat4* world = &transforms[i].worldMatrix;
            FluxionRenderLight* light = &out[*written];
            memset(light, 0, sizeof(*light));

            light->position = Fluxion_SceneLight_Position(world);
            light->direction = Fluxion_SceneLight_Forward(world);

            if (lightType == Fluxion_DirectionalLight_TypeId())
            {
                const FluxionDirectionalLight* source = &((const FluxionDirectionalLight*)lights)[i];
                light->type = FLUXION_RENDER_LIGHT_DIRECTIONAL;
                light->color = source->color;
            }
            else if (lightType == Fluxion_PointLight_TypeId())
            {
                const FluxionPointLight* source = &((const FluxionPointLight*)lights)[i];
                light->type = FLUXION_RENDER_LIGHT_POINT;
                light->color = source->color;
                light->range = source->range;
            }
            else
            {
                const FluxionSpotLight* source = &((const FluxionSpotLight*)lights)[i];
                light->type = FLUXION_RENDER_LIGHT_SPOT;
                light->color = source->color;
                light->range = source->range;

                // The cosines, taken here, once. A shader would otherwise
                // take them per pixel per light, and the angle itself is
                // never wanted for anything.
                //
                // The inner is clamped to stay above the outer: an inner
                // cone wider than its outer one gives a negative span,
                // and the smooth step across it would come out inverted
                // -- a spot light dark in the middle and bright at the
                // rim, which reads as a shader bug rather than as two
                // angles the wrong way round.
                f32 outer = cosf(source->outerConeAngle);
                f32 inner = cosf(source->innerConeAngle);
                if (inner < outer) inner = outer;

                light->innerConeCos = inner;
                light->outerConeCos = outer;
            }

            ++(*written);
        }
    }
}

u32 Fluxion_Scene_GatherLights(FluxionSceneHandle scene, FluxionRenderLight* outLights, u32 capacity)
{
    u32 written = 0;
    u32 total = 0;

    if (outLights == NULL) capacity = 0;

    Fluxion_SceneLight_GatherOne(scene, Fluxion_DirectionalLight_TypeId(), outLights, capacity, &written, &total);
    Fluxion_SceneLight_GatherOne(scene, Fluxion_PointLight_TypeId(), outLights, capacity, &written, &total);
    Fluxion_SceneLight_GatherOne(scene, Fluxion_SpotLight_TypeId(), outLights, capacity, &written, &total);

    // How many there ARE, not how many fitted. A caller sizes its next
    // call from this, so answering with what it could hold would mean it
    // never learned it needed more.
    return total;
}

bool Fluxion_Scene_GatherEnvironment(FluxionSceneHandle scene, FluxionEnvironmentLight* outEnvironment)
{
    if (outEnvironment == NULL) return false;

    const FluxionTypeId required[1] = { Fluxion_EnvironmentLight_TypeId() };

    FluxionEntityQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.required = required;
    desc.requiredCount = 1;

    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;

    bool found = false;
    u32 total = 0;

    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        const FluxionEnvironmentLight* column =
            (const FluxionEnvironmentLight*)Fluxion_EntityChunk_Column(&chunk, Fluxion_EnvironmentLight_TypeId());
        if (column == NULL) continue;

        total += chunk.count;

        if (!found && chunk.count > 0)
        {
            *outEnvironment = column[0];
            found = true;
        }
    }

    // Reported rather than passed over. A scene is inside one world, and
    // two skies has no answer -- so picking one silently would make which
    // sky you got depend on the order the objects happened to be created
    // in, which is not something anyone can look at and check.
    if (total > 1)
    {
        FLUXION_LOG_WARN("Scene", "This scene has %u environments and can only be inside one; the first found is used.", total);
    }

    return found;
}
