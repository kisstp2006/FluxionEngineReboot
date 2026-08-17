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

// The camera component: registration, and the two matrices a view takes.

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Scene/Camera.h>
#include <Fluxion/Scene/EntityQuery.h>

#include <math.h>
#include <string.h>

#define FLUXION_SCENE_CAMERA_PROPERTY_COUNT 3

static FluxionPropertyInfo s_cameraProperties[FLUXION_SCENE_CAMERA_PROPERTY_COUNT];
static FluxionTypeInfo s_cameraType;

FluxionTypeId Fluxion_Camera_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionCamera); }

bool Fluxion_SceneCamera_EnsureRegistered(void)
{
    if (!Fluxion_Reflection_IsInitialized()) return false;
    if (Fluxion_Reflection_FindTypeById(Fluxion_Camera_TypeId()) != NULL) return true;

    const FluxionPropertyInfo properties[FLUXION_SCENE_CAMERA_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionCamera, fovYRadians, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionCamera, nearPlane, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionCamera, farPlane, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_cameraProperties, properties, sizeof(properties));

    s_cameraType.name = Fluxion_StringView_FromCStr("FluxionCamera");
    s_cameraType.id = Fluxion_Camera_TypeId();
    s_cameraType.kind = FLUXION_TYPE_KIND_STRUCT;
    s_cameraType.size = (u32)sizeof(FluxionCamera);
    s_cameraType.version = 1;
    s_cameraType.members = Fluxion_Span_Make(s_cameraProperties, FLUXION_SCENE_CAMERA_PROPERTY_COUNT, sizeof(FluxionPropertyInfo));
    s_cameraType.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

    return Fluxion_Reflection_RegisterType(&s_cameraType);
}

// The standard perspective matrix, depth mapped to 0..1.
//
// ZERO AT THE NEAR PLANE, ONE AT THE FAR ONE, which is what two of the
// three backends here take natively and what the third is told to take
// (see the OpenGL backend's clip control). The other convention, -1..1,
// is what this matrix used to produce -- and on a backend expecting
// 0..1 it threw away everything in the near half of the range, which
// does not look like an error, only like less precision than there
// should be.
static FluxionMat4 Fluxion_SceneCamera_Perspective(f32 fovYRadians, f32 aspect, f32 nearPlane, f32 farPlane)
{
    FluxionMat4 m;
    memset(&m, 0, sizeof(m));

    const f32 f = 1.0f / tanf(fovYRadians * 0.5f);
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = farPlane / (nearPlane - farPlane);
    m.m[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    m.m[3][2] = -1.0f;
    return m;
}

bool Fluxion_Scene_GatherCamera(FluxionSceneHandle scene, f32 aspect, FluxionMat4* outView, FluxionMat4* outProjection)
{
    if (outView == NULL || outProjection == NULL) return false;
    if (aspect <= 0.0f) aspect = 1.0f;

    const FluxionTypeId required[2] = { Fluxion_Camera_TypeId(), Fluxion_Transform_TypeId() };

    FluxionEntityQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.required = required;
    desc.requiredCount = 2;

    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;

    bool found = false;
    u32 total = 0;

    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        const FluxionCamera* cameras = (const FluxionCamera*)Fluxion_EntityChunk_Column(&chunk, Fluxion_Camera_TypeId());
        const FluxionTransform* transforms = (const FluxionTransform*)Fluxion_EntityChunk_Column(&chunk, Fluxion_Transform_TypeId());
        if (cameras == NULL || transforms == NULL) continue;

        total += chunk.count;

        if (!found && chunk.count > 0)
        {
            // The view undoes where the camera stands: the world matrix
            // takes camera space to the world, the view is its inverse.
            // Rigid, because a camera's transform is a place and a
            // direction -- a scaled camera is not a thing this supports.
            *outView = Fluxion_Mat4_RigidInverse(transforms[0].worldMatrix);

            const FluxionCamera* camera = &cameras[0];
            const f32 fov = camera->fovYRadians > 0.0f ? camera->fovYRadians : 1.0472f;
            const f32 nearPlane = camera->nearPlane > 0.0f ? camera->nearPlane : 0.1f;
            const f32 farPlane = camera->farPlane > nearPlane ? camera->farPlane : nearPlane + 100.0f;
            *outProjection = Fluxion_SceneCamera_Perspective(fov, aspect, nearPlane, farPlane);
            found = true;
        }
    }

    // One scene, one eye. Reported rather than silently picked, same as
    // the environment: which camera won must not depend on creation order
    // nobody can see.
    if (total > 1)
    {
        FLUXION_LOG_WARN("Scene", "This scene has %u cameras and a view can only look through one; the first found is used.", total);
    }

    return found;
}
