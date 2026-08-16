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

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Containers/DynamicArray.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Scene/SceneSerialization.h>

#include <string.h>

// Which assets a scene will reach for.
//
// Not for loading them -- for KNOWING them. Whoever packages a build has
// to be able to say which assets a scene requires, and a scene does not
// announce that anywhere: it is spread across the fields of its objects'
// components. So this is the one place that asks, and it asks the only
// thing that can answer, which is what each field's declared type is.
//
// This is why an asset reference has a registered type at all. It does
// not need one in order to be written down -- being an id rather than a
// handle, it survives that on its own -- it needs one in order to be
// recognised.

typedef struct FluxionSceneAssetGather
{
    // Collected here rather than straight into the caller's buffer, so
    // that telling one asset from twenty uses of it does not depend on
    // how much room the caller brought. Counting into a buffer that is
    // absent or too small would make "how many are there" answer
    // differently depending on who asked, and the whole point of the
    // count is to size the next call.
    FluxionDynamicArray found;
} FluxionSceneAssetGather;

static void Fluxion_SceneAssets_Note(FluxionSceneAssetGather* gather, FluxionAssetRef ref)
{
    usize i;

    if (!Fluxion_AssetRef_IsSet(ref)) return;

    // One asset used by twenty objects is one asset.
    for (i = 0; i < gather->found.count; ++i)
    {
        const FluxionUUID* seen = (const FluxionUUID*)Fluxion_DynamicArray_At(&gather->found, i);
        if (Fluxion_UUID_Equals(*seen, ref.asset)) return;
    }

    Fluxion_DynamicArray_Push(&gather->found, &ref.asset);
}

static void Fluxion_SceneAssets_FromComponent(FluxionSceneAssetGather* gather, const FluxionTypeInfo* typeInfo, void* instance)
{
    const FluxionTypeId assetRefType = Fluxion_AssetRef_TypeId();
    usize i;

    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        FluxionAssetRef ref;

        if (property->type != assetRefType) continue;

        // A field that says it is a reference but is not the size of one
        // is a disagreement, not a reference. Reading it as one would put
        // whatever is next to it into an id.
        if (property->size != sizeof(FluxionAssetRef)) continue;

        if (property->accessKind == FLUXION_PROPERTY_ACCESS_OFFSET)
        {
            memcpy(&ref, (const u8*)instance + property->offset, sizeof(ref));
        }
        else
        {
            if (property->accessor.getter == NULL) continue;
            property->accessor.getter(instance, &ref, property->accessor.context);
        }

        Fluxion_SceneAssets_Note(gather, ref);
    }
}

static void Fluxion_SceneAssets_FromObject(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionSceneAssetGather* gather)
{
    FluxionTypeId types[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    FluxionGameObjectHandle child;
    u32 typeCount;
    u32 i;

    typeCount = Fluxion_GameObject_GetComponentTypes(scene, object, types, FLUXION_SCENE_MAX_COMPONENT_TYPES);

    for (i = 0; i < typeCount; ++i)
    {
        const FluxionTypeInfo* typeInfo = Fluxion_Reflection_FindTypeById(types[i]);
        void* instance;

        if (typeInfo == NULL) continue;

        instance = Fluxion_GameObject_GetComponent(scene, object, types[i]);
        if (instance == NULL) continue;

        Fluxion_SceneAssets_FromComponent(gather, typeInfo, instance);
    }

    for (child = Fluxion_GameObject_GetFirstChild(scene, object); FLUXION_HANDLE_IS_VALID(child);
         child = Fluxion_GameObject_GetNextSibling(scene, child))
    {
        Fluxion_SceneAssets_FromObject(scene, child, gather);
    }
}

u32 Fluxion_Scene_GatherAssetReferences(FluxionSceneHandle scene, FluxionUUID* outAssets, u32 capacity)
{
    FluxionSceneAssetGather gather;
    FluxionGameObjectHandle root;
    u32 total;
    u32 written;
    u32 i;

    if (!Fluxion_Scene_IsValid(scene)) return 0;

    Fluxion_DynamicArray_Init(&gather.found, Fluxion_DefaultAllocator(), sizeof(FluxionUUID));

    for (root = Fluxion_Scene_GetFirstRoot(scene); FLUXION_HANDLE_IS_VALID(root);
         root = Fluxion_GameObject_GetNextSibling(scene, root))
    {
        Fluxion_SceneAssets_FromObject(scene, root, &gather);
    }

    total = (u32)gather.found.count;
    written = (outAssets != NULL && capacity < total) ? capacity : (outAssets != NULL ? total : 0u);

    for (i = 0; i < written; ++i)
    {
        outAssets[i] = *(const FluxionUUID*)Fluxion_DynamicArray_At(&gather.found, i);
    }

    Fluxion_DynamicArray_Destroy(&gather.found);
    return total;
}
