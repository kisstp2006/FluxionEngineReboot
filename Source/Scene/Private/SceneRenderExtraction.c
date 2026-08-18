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

// The scene, once a frame, in the shape the renderer takes.

#include "SceneInternal.h"

#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/MaterialAsset.h>
#include <Fluxion/RenderCore/Renderer/MeshAsset.h>
#include <Fluxion/Scene/Camera.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Light.h>
#include <Fluxion/Scene/MeshRenderer.h>
#include <Fluxion/Scene/Transform.h>

#include <string.h>

#define FLUXION_SCENE_EXTRACTION_LOG_CATEGORY "RenderExtraction"

#define FLUXION_SCENE_MESH_RENDERER_PROPERTY_COUNT 3

static FluxionPropertyInfo s_meshRendererProperties[FLUXION_SCENE_MESH_RENDERER_PROPERTY_COUNT];
static FluxionTypeInfo s_meshRendererType;

FluxionTypeId Fluxion_MeshRenderer_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionMeshRenderer); }

bool Fluxion_SceneMeshRenderer_EnsureRegistered(void)
{
    if (!Fluxion_Reflection_IsInitialized()) return false;
    if (Fluxion_Reflection_FindTypeById(Fluxion_MeshRenderer_TypeId()) != NULL) return true;

    const FluxionPropertyInfo properties[FLUXION_SCENE_MESH_RENDERER_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionMeshRenderer, mesh, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionMeshRenderer, material, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionMeshRenderer, layerMask, FLUXION_TYPE_ID_OF(u32), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_meshRendererProperties, properties, sizeof(properties));

    s_meshRendererType.name = Fluxion_StringView_FromCStr("FluxionMeshRenderer");
    s_meshRendererType.id = Fluxion_MeshRenderer_TypeId();
    s_meshRendererType.kind = FLUXION_TYPE_KIND_STRUCT;
    s_meshRendererType.size = (u32)sizeof(FluxionMeshRenderer);
    s_meshRendererType.version = 1;
    s_meshRendererType.members = Fluxion_Span_Make(s_meshRendererProperties, FLUXION_SCENE_MESH_RENDERER_PROPERTY_COUNT, sizeof(FluxionPropertyInfo));
    s_meshRendererType.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

    return Fluxion_Reflection_RegisterType(&s_meshRendererType);
}

// --- The assets a scene is holding on to ----------------------------------
//
// One entry per asset any of this scene's components points at, acquired
// the first time it is seen and held until the scene goes.
//
// Per frame would be wrong twice over: an acquire and a release each way
// for every object every frame, and -- worse -- an asset whose last
// reference went at the end of one frame would be UNLOADED and loaded
// again for the next one.
//
// WHAT THIS DOES NOT DO is let go of an asset that no component points at
// any more. A long editing session that swapped every material would keep
// both sets. Said rather than left to be discovered; the way out is
// Fluxion_Scene_ReleaseRenderAssets, which a caller may use.

#define FLUXION_SCENE_MAX_RESIDENT_ASSETS 256

typedef struct FluxionSceneResidentAsset
{
    FluxionUUID id;
    FluxionAssetHandle handle;
} FluxionSceneResidentAsset;

typedef struct FluxionSceneResidentAssets
{
    FluxionSceneResidentAsset entries[FLUXION_SCENE_MAX_RESIDENT_ASSETS];
    u32 count;
} FluxionSceneResidentAssets;

static FluxionSceneResidentAssets s_residentAssets[FLUXION_SCENE_MAX_SCENES];

static FluxionAssetHandle Fluxion_SceneExtractionInternal_Acquire(u32 sceneIndex, FluxionAssetRef ref)
{
    const FluxionAssetHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_AssetRef_IsSet(ref)) return invalid;

    FluxionSceneResidentAssets* resident = &s_residentAssets[sceneIndex];
    for (u32 i = 0; i < resident->count; ++i)
    {
        if (Fluxion_UUID_Equals(resident->entries[i].id, ref.asset)) return resident->entries[i].handle;
    }

    if (resident->count >= FLUXION_SCENE_MAX_RESIDENT_ASSETS)
    {
        FLUXION_LOG_ERROR(FLUXION_SCENE_EXTRACTION_LOG_CATEGORY,
                          "this scene already holds %d assets; whatever asked for another is not drawn", FLUXION_SCENE_MAX_RESIDENT_ASSETS);
        return invalid;
    }

    const FluxionAssetHandle handle = Fluxion_Assets_AcquireRef(ref);
    if (!FLUXION_HANDLE_IS_VALID(handle)) return invalid;

    resident->entries[resident->count].id = ref.asset;
    resident->entries[resident->count].handle = handle;
    ++resident->count;
    return handle;
}

void Fluxion_Scene_ReleaseRenderAssets(FluxionSceneHandle scene)
{
    if (scene.index >= FLUXION_SCENE_MAX_SCENES) return;

    FluxionSceneResidentAssets* resident = &s_residentAssets[scene.index];
    for (u32 i = 0; i < resident->count; ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(resident->entries[i].handle)) Fluxion_Assets_Release(resident->entries[i].handle);
    }
    resident->count = 0;
}

// --- The walk -------------------------------------------------------------

static bool Fluxion_SceneExtractionInternal_ResolveMesh(u32 sceneIndex, FluxionAssetRef ref, FluxionMeshBufferHandle* outMesh)
{
    const FluxionAssetHandle handle = Fluxion_SceneExtractionInternal_Acquire(sceneIndex, ref);
    if (!FLUXION_HANDLE_IS_VALID(handle)) return false;
    if (Fluxion_Assets_GetState(handle) != FLUXION_ASSET_STATE_READY) return false;

    const FluxionMeshAsset* asset = (const FluxionMeshAsset*)Fluxion_Assets_GetObject(handle);
    if (asset == NULL || !FLUXION_HANDLE_IS_VALID(asset->buffer)) return false;

    *outMesh = asset->buffer;
    return true;
}

static bool Fluxion_SceneExtractionInternal_ResolveMaterial(u32 sceneIndex, FluxionAssetRef ref, FluxionMaterialHandle* outMaterial,
                                                            FluxionRenderPipelineHandle* outPipeline)
{
    const FluxionAssetHandle handle = Fluxion_SceneExtractionInternal_Acquire(sceneIndex, ref);
    if (!FLUXION_HANDLE_IS_VALID(handle)) return false;
    if (Fluxion_Assets_GetState(handle) != FLUXION_ASSET_STATE_READY) return false;

    const FluxionMaterialAsset* asset = (const FluxionMaterialAsset*)Fluxion_Assets_GetObject(handle);
    if (asset == NULL || !FLUXION_HANDLE_IS_VALID(asset->material) || !FLUXION_HANDLE_IS_VALID(asset->pipeline)) return false;

    *outMaterial = asset->material;
    *outPipeline = asset->pipeline;
    return true;
}

bool Fluxion_Scene_ExtractRenderWorld(FluxionSceneHandle scene, f32 aspect, FluxionRenderWorld* world)
{
    if (world == NULL) return false;
    if (scene.index >= FLUXION_SCENE_MAX_SCENES) return false;

    Fluxion_RenderWorld_Clear(world);

    // The lights and the camera first, through the calls that already
    // answer those questions: this step is about where the answers GO,
    // not about answering them again.
    world->lightsInWorld = Fluxion_Scene_GatherLights(scene, world->lights, FLUXION_RENDER_WORLD_MAX_LIGHTS);
    world->lightCount = world->lightsInWorld < FLUXION_RENDER_WORLD_MAX_LIGHTS ? world->lightsInWorld : FLUXION_RENDER_WORLD_MAX_LIGHTS;
    if (world->lightsInWorld > world->lightCount)
    {
        FLUXION_LOG_WARN(FLUXION_SCENE_EXTRACTION_LOG_CATEGORY,
                         "this scene has %u lights and a frame holds %u; the rest light nothing", world->lightsInWorld, world->lightCount);
    }

    world->camera.valid = Fluxion_Scene_GatherCamera(scene, aspect, &world->camera.view, &world->camera.projection);

    // WHERE THE EYE IS, which is what chooses each object's level of
    // detail below. Taken out of the view matrix rather than out of the
    // camera's transform: the view matrix is what the frame is actually
    // drawn with, and a camera whose transform said one thing while its
    // view said another would pick levels for a place nothing is drawn
    // from.
    //
    // Undone by the same call the render view uses to answer the same
    // question -- two ways of inverting a view matrix would eventually
    // be two different eyes.
    FluxionVec3 eye = { 0.0f, 0.0f, 0.0f };
    if (world->camera.valid)
    {
        const FluxionMat4 cameraToWorld = Fluxion_Mat4_RigidInverse(world->camera.view);
        eye.x = cameraToWorld.m[0][3];
        eye.y = cameraToWorld.m[1][3];
        eye.z = cameraToWorld.m[2][3];
    }

    const FluxionTypeId required[2] = { Fluxion_MeshRenderer_TypeId(), Fluxion_Transform_TypeId() };

    FluxionEntityQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.required = required;
    desc.requiredCount = 2;

    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;

    u32 waiting = 0;
    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        const FluxionMeshRenderer* renderers = (const FluxionMeshRenderer*)Fluxion_EntityChunk_Column(&chunk, Fluxion_MeshRenderer_TypeId());
        const FluxionTransform* transforms = (const FluxionTransform*)Fluxion_EntityChunk_Column(&chunk, Fluxion_Transform_TypeId());
        if (renderers == NULL || transforms == NULL) continue;

        for (u32 i = 0; i < chunk.count; ++i)
        {
            FluxionRenderObject object;
            memset(&object, 0, sizeof(object));

            if (!Fluxion_SceneExtractionInternal_ResolveMesh(scene.index, renderers[i].mesh, &object.mesh) ||
                !Fluxion_SceneExtractionInternal_ResolveMaterial(scene.index, renderers[i].material, &object.material, &object.pipeline))
            {
                // Not ready, or not there at all. Left out whole rather
                // than drawn with a mesh that happens to be loaded --
                // and counted, so that "nothing appeared" has a number
                // behind it.
                ++waiting;
                continue;
            }

            object.transform = transforms[i].worldMatrix;
            object.layerMask = renderers[i].layerMask != 0 ? renderers[i].layerMask : 0xFFFFFFFFu;

            // WHICH LEVEL OF DETAIL, decided here and nowhere else.
            //
            // Here because this is the one place that knows both where
            // the object is and where the eye is, and because the answer
            // has to be known before the batching -- a batch is one draw
            // command, and a command carries one range of indices.
            //
            // The same on both culling paths: the device decides what is
            // SEEN, never what is drawn at which detail, because that
            // choice is part of what makes a batch.
            object.lodIndex = Fluxion_MeshBuffer_SelectLevel(object.mesh, eye, &object.transform);

            // Seen. What decides otherwise is the culling, which happens
            // where the frame is assembled rather than here.
            object.visible = true;

            if (!Fluxion_RenderWorld_AddObject(world, &object)) break;
        }
    }

    if (waiting > 0)
    {
        FLUXION_LOG_INFO(FLUXION_SCENE_EXTRACTION_LOG_CATEGORY,
                         "%u object(s) are waiting for their mesh or material and were not drawn this frame", waiting);
    }

    return world->camera.valid;
}
