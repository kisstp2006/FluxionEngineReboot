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

// The cooked half of a render graph asset, its registration with the
// asset system, and the step that turns one into a real graph.

#include <Fluxion/RenderCore/Pipeline/RenderGraphAsset.h>

#include "RenderGraph/RenderGraphInternal.h"

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <string.h>

#define FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY "RenderGraphAsset"

// An asset that the runtime could read and then not build would be a
// failure arriving one step late, so the two capacities are one number
// said in two places -- and this is what keeps them one.
static_assert(FLUXION_RENDER_GRAPH_ASSET_MAX_NODES <= FLUXION_RENDER_GRAPH_MAX_NODES,
              "Fluxion: a render graph asset may not hold more nodes than a render graph can");
static_assert(FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH == FLUXION_RENDER_GRAPH_MAX_NAME_LENGTH,
              "Fluxion: an asset's names become the runtime's names and must be able to hold the same ones");

static bool s_registered = false;

FluxionAssetTypeId Fluxion_RenderGraphAsset_TypeId(void)
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(FLUXION_RENDER_GRAPH_ASSET_TYPE_NAME));
}

// ---------------------------------------------------------------------
// The format, both directions in one place.
// ---------------------------------------------------------------------

// Length-prefixed rather than a fixed field: the capacity is this
// build's business and the file's business is what the name actually is,
// so growing the limit later does not invalidate what was written.
static bool Fluxion_RenderGraphAsset_SerializeName(FluxionStream* stream, char* name, usize capacity)
{
    u32 length = Fluxion_Stream_IsWriting(stream) ? (u32)strlen(name) : 0;
    Fluxion_Stream_SerializeU32(stream, &length);

    // Checked before the bytes are touched in either direction: on the
    // way in this refuses a file claiming a name this build cannot hold,
    // and it does so without reading it.
    if ((usize)length >= capacity) return false;

    Fluxion_Stream_SerializeBytes(stream, name, length);
    if (Fluxion_Stream_IsReading(stream)) name[length] = '\0';
    return true;
}

static bool Fluxion_RenderGraphAsset_Serialize(FluxionStream* stream, FluxionRenderGraphAsset* asset)
{
    u32 magic = FLUXION_RENDER_GRAPH_ASSET_MAGIC;
    u32 formatVersion = FLUXION_RENDER_GRAPH_ASSET_FORMAT_VERSION;
    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    if (magic != FLUXION_RENDER_GRAPH_ASSET_MAGIC)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "these are not the bytes of a render graph");
        return false;
    }
    if (formatVersion > FLUXION_RENDER_GRAPH_ASSET_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                          "this render graph was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    if (!Fluxion_RenderGraphAsset_SerializeName(stream, asset->name, sizeof(asset->name))) return false;

    Fluxion_Stream_SerializeU32(stream, &asset->importCount);
    if (asset->importCount > FLUXION_RENDER_GRAPH_ASSET_MAX_IMPORTS)
    {
        asset->importCount = 0;
        return false;
    }
    for (u32 i = 0; i < asset->importCount; ++i)
    {
        if (!Fluxion_RenderGraphAsset_SerializeName(stream, asset->imports[i].name, sizeof(asset->imports[i].name))) return false;

        u32 kind = (u32)asset->imports[i].kind;
        Fluxion_Stream_SerializeU32(stream, &kind);
        if (kind != FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE && kind != FLUXION_RENDER_GRAPH_ASSET_IMPORT_BUFFER) return false;
        asset->imports[i].kind = (FluxionRenderGraphAssetImportKind)kind;
    }

    Fluxion_Stream_SerializeU32(stream, &asset->nodeCount);
    if (asset->nodeCount > FLUXION_RENDER_GRAPH_ASSET_MAX_NODES)
    {
        asset->nodeCount = 0;
        return false;
    }
    for (u32 i = 0; i < asset->nodeCount; ++i)
    {
        if (!Fluxion_RenderGraphAsset_SerializeName(stream, asset->nodes[i].name, sizeof(asset->nodes[i].name))) return false;
        if (!Fluxion_RenderGraphAsset_SerializeName(stream, asset->nodes[i].passType, sizeof(asset->nodes[i].passType))) return false;
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_RenderGraphAsset_Write(FluxionStream* stream, const FluxionRenderGraphAsset* asset)
{
    if (stream == NULL || asset == NULL || !Fluxion_Stream_IsWriting(stream)) return false;

    // Copied because the shared serializer takes one non-const pointer
    // for both directions; the write direction only ever reads from it.
    FluxionRenderGraphAsset copy = *asset;
    return Fluxion_RenderGraphAsset_Serialize(stream, &copy);
}

bool Fluxion_RenderGraphAsset_Read(const u8* bytes, usize size, FluxionRenderGraphAsset** outAsset)
{
    if (bytes == NULL || outAsset == NULL) return false;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionRenderGraphAsset* asset =
        (FluxionRenderGraphAsset*)Fluxion_Allocator_Alloc(allocator, sizeof(FluxionRenderGraphAsset), FLUXION_DEFAULT_ALIGNMENT);
    if (asset == NULL) return false;

    memset(asset, 0, sizeof(*asset));

    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, bytes, size);

    if (!Fluxion_RenderGraphAsset_Serialize(&stream, asset))
    {
        Fluxion_Allocator_Free(allocator, asset, sizeof(FluxionRenderGraphAsset));
        return false;
    }

    *outAsset = asset;
    return true;
}

void Fluxion_RenderGraphAsset_Destroy(FluxionRenderGraphAsset* asset)
{
    if (asset == NULL) return;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), asset, sizeof(FluxionRenderGraphAsset));
}

// ---------------------------------------------------------------------
// The asset system's two halves.
// ---------------------------------------------------------------------

static bool Fluxion_RenderGraphAsset_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    FLUXION_UNUSED(userData);

    FluxionRenderGraphAsset* asset = NULL;
    if (!Fluxion_RenderGraphAsset_Read(bytes, size, &asset)) return false;

    *outObject = asset;
    return true;
}

static void Fluxion_RenderGraphAsset_Unload(void* object, void* userData)
{
    FLUXION_UNUSED(userData);
    Fluxion_RenderGraphAsset_Destroy((FluxionRenderGraphAsset*)object);
}

bool Fluxion_RenderGraphAsset_RegisterType(void)
{
    if (s_registered) return true;

    FluxionAssetTypeDesc desc;
    memset(&desc, 0, sizeof(desc));

    memcpy(desc.name, FLUXION_RENDER_GRAPH_ASSET_TYPE_NAME, sizeof(FLUXION_RENDER_GRAPH_ASSET_TYPE_NAME));
    memcpy(desc.cookedExtension, "fluxrendergraph", sizeof("fluxrendergraph"));

    // No source extension and no import function here, the same way the
    // mesh and the texture types have none: the authored form is read by
    // Fluxion_RenderGraphAsset_ParseText, and whoever cooks a project
    // calls that and Write. What a shipped game loads is the cooked form
    // alone, and it needs no JSON reader to do it.
    desc.sourceExtensionCount = 0;
    desc.import = NULL;

    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = Fluxion_RenderGraphAsset_Load;

    // Nothing to hand a device. A list of pass names is finished the
    // moment it is read, and a type that left this filled in would make
    // every graph stop at a state that means nothing for it.
    desc.finalize = NULL;
    desc.unload = Fluxion_RenderGraphAsset_Unload;

    s_registered = Fluxion_AssetTypes_Register(&desc);
    return s_registered;
}

void Fluxion_RenderGraphAsset_UnregisterType(void)
{
    if (!s_registered) return;

    Fluxion_AssetTypes_Unregister(Fluxion_RenderGraphAsset_TypeId());
    s_registered = false;
}

// ---------------------------------------------------------------------
// Turning one into a real graph.
// ---------------------------------------------------------------------

static const FluxionRenderGraphBinding* Fluxion_RenderGraphAsset_FindBinding(const FluxionRenderGraphInstantiateDesc* desc, const char* name)
{
    for (u32 i = 0; i < desc->bindingCount; ++i)
    {
        if (desc->bindings[i].name != NULL && strcmp(desc->bindings[i].name, name) == 0) return &desc->bindings[i];
    }
    return NULL;
}

static bool Fluxion_RenderGraphAsset_DeclaresImport(const FluxionRenderGraphAsset* asset, const char* name)
{
    for (u32 i = 0; i < asset->importCount; ++i)
    {
        if (strcmp(asset->imports[i].name, name) == 0) return true;
    }
    return false;
}

// Everything that can be answered without touching the graph, answered
// first -- so a rejected instantiation leaves a graph nobody has half
// built.
static bool Fluxion_RenderGraphAsset_Validate(const FluxionRenderGraphAsset* asset, const FluxionRenderGraphInstantiateDesc* desc)
{
    for (u32 i = 0; i < desc->bindingCount; ++i)
    {
        const char* name = desc->bindings[i].name;
        if (name == NULL)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "\"%s\": a binding has no name", asset->name);
            return false;
        }
        if (!Fluxion_RenderGraphAsset_DeclaresImport(asset, name))
        {
            // A misspelt binding would otherwise import a resource under
            // a name no pass ever reads, and the frame would draw with
            // whatever the real name still resolved to.
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "\"%s\" was given a binding for \"%s\", which it does not declare as an import", asset->name, name);
            return false;
        }
    }

    for (u32 i = 0; i < asset->importCount; ++i)
    {
        const FluxionRenderGraphAssetImport* import = &asset->imports[i];
        const FluxionRenderGraphBinding* binding = Fluxion_RenderGraphAsset_FindBinding(desc, import->name);
        if (binding == NULL)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "\"%s\" declares the import \"%s\" and nothing was bound to it", asset->name, import->name);
            return false;
        }

        const bool valid = import->kind == FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE
                               ? FLUXION_HANDLE_IS_VALID(binding->texture)
                               : FLUXION_HANDLE_IS_VALID(binding->buffer);
        if (!valid)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "\"%s\" declares \"%s\" as a %s, and the binding for it holds no such handle", asset->name, import->name,
                              import->kind == FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE ? "texture" : "buffer");
            return false;
        }
    }

    for (u32 i = 0; i < asset->nodeCount; ++i)
    {
        if (Fluxion_RenderGraphPassRegistry_Find(asset->nodes[i].passType) == NULL)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "\"%s\" asks for the pass type \"%s\", which nothing registered", asset->name, asset->nodes[i].passType);
            return false;
        }
    }

    return true;
}

bool Fluxion_RenderGraphAsset_Instantiate(const FluxionRenderGraphAsset* asset, FluxionRenderGraph* graph,
                                          const FluxionRenderGraphInstantiateDesc* desc)
{
    if (asset == NULL || graph == NULL || desc == NULL) return false;
    if (desc->bindingCount > 0 && desc->bindings == NULL) return false;

    if (!Fluxion_RenderGraphAsset_Validate(asset, desc)) return false;

    for (u32 i = 0; i < asset->importCount; ++i)
    {
        const FluxionRenderGraphAssetImport* import = &asset->imports[i];
        const FluxionRenderGraphBinding* binding = Fluxion_RenderGraphAsset_FindBinding(desc, import->name);

        if (import->kind == FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE)
        {
            Fluxion_RenderGraph_ImportTexture(graph, import->name, binding->texture, binding->currentState);
        }
        else
        {
            Fluxion_RenderGraph_ImportBuffer(graph, import->name, binding->buffer, binding->currentState);
        }
    }

    for (u32 i = 0; i < asset->nodeCount; ++i)
    {
        const FluxionRenderGraphAssetNode* node = &asset->nodes[i];

        void* userData = desc->resolveUserData != NULL ? desc->resolveUserData(node->passType, node->name, desc->context)
                                                       : desc->context;

        const FluxionRenderGraphPassHandle handle = Fluxion_RenderGraph_AddPassFromRegistry(graph, node->passType, userData);

        // The pass type was found in the registry a moment ago and the
        // asset can hold no more nodes than a graph can, so the only way
        // here is a graph that was not freshly created -- which the
        // contract forbids.
        FLUXION_ASSERT(FLUXION_HANDLE_IS_VALID(handle));
        if (!FLUXION_HANDLE_IS_VALID(handle)) return false;

        memcpy(graph->nodes[handle.index].name, node->name, strlen(node->name) + 1);
    }

    return true;
}
