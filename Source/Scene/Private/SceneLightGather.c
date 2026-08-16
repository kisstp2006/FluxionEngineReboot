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
