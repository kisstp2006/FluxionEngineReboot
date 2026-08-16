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

#include "TestFramework.h"

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/RenderCore/Renderer/MeshAsset.h>

#include <cstdio>
#include <cstring>
#include <vector>

// The engine's own form of a mesh, written and read back with no device
// involved. This is the half that ships, so it is the half that has to
// hold up on its own.

namespace
{

struct Vertex
{
    float position[3];
    float normal[3];
};

FluxionMeshAssetData MakeCube(const std::vector<Vertex>& vertices, const std::vector<u16>& indices, FluxionRHIVertexLayout& layout)
{
    std::memset(&layout, 0, sizeof(layout));
    layout.attributeCount = 2;
    layout.stride = sizeof(Vertex);
    layout.attributes[0].location = 0;
    layout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    layout.attributes[0].offset = 0;
    layout.attributes[1].location = 1;
    layout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    layout.attributes[1].offset = sizeof(float) * 3;

    FluxionMeshAssetData data{};
    data.vertexData = vertices.data();
    data.vertexDataSize = vertices.size() * sizeof(Vertex);
    data.indexData = indices.data();
    data.indexDataSize = indices.size() * sizeof(u16);
    data.use16BitIndices = true;
    data.vertexLayout = layout;
    data.bounds.min = FluxionVec3{ -1.0f, -2.0f, -3.0f };
    data.bounds.max = FluxionVec3{ 1.0f, 2.0f, 3.0f };
    return data;
}

void RoundTrip(TestContext* ctx)
{
    const std::vector<Vertex> vertices = {
        { { -1.0f, -2.0f, -3.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 2.0f, 3.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.25f }, { 1.0f, 0.0f, 0.0f } },
    };
    const std::vector<u16> indices = { 0, 1, 2 };

    FluxionRHIVertexLayout layout;
    const FluxionMeshAssetData data = MakeCube(vertices, indices, layout);

    std::vector<u8> bytes(4096, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, bytes.data(), bytes.size());
    TEST_CHECK(ctx, Fluxion_MeshAsset_Write(&writer, &data));
    const usize written = Fluxion_Stream_GetPosition(&writer);

    FluxionMeshAsset* asset = nullptr;
    TEST_CHECK(ctx, Fluxion_MeshAsset_Read(bytes.data(), written, &asset));
    TEST_CHECK(ctx, asset != nullptr);
    if (!asset) return;

    // Nothing has been given to a device, so there is no buffer -- and
    // that has to be said, because index zero is a real buffer.
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->buffer));

    TEST_CHECK(ctx, asset->use16BitIndices);
    TEST_CHECK(ctx, asset->indexCount == 3);
    TEST_CHECK(ctx, asset->vertexDataSize == data.vertexDataSize);
    TEST_CHECK(ctx, asset->indexDataSize == data.indexDataSize);

    TEST_CHECK(ctx, asset->vertexLayout.attributeCount == 2);
    TEST_CHECK(ctx, asset->vertexLayout.stride == sizeof(Vertex));
    TEST_CHECK(ctx, asset->vertexLayout.attributes[1].location == 1);
    TEST_CHECK(ctx, asset->vertexLayout.attributes[1].offset == sizeof(float) * 3);
    TEST_CHECK(ctx, asset->vertexLayout.attributes[1].format == FLUXION_RHI_FORMAT_R32G32B32_FLOAT);

    TEST_CHECK(ctx, asset->bounds.min.x == -1.0f && asset->bounds.min.z == -3.0f);
    TEST_CHECK(ctx, asset->bounds.max.y == 2.0f);

    TEST_CHECK(ctx, asset->vertexData != nullptr && std::memcmp(asset->vertexData, vertices.data(), data.vertexDataSize) == 0);
    TEST_CHECK(ctx, asset->indexData != nullptr && std::memcmp(asset->indexData, indices.data(), data.indexDataSize) == 0);

    // Written again from what was read. Byte-identical or something
    // landed in the wrong place -- which every value checked above can
    // still look right through.
    FluxionMeshAssetData again{};
    again.vertexData = asset->vertexData;
    again.vertexDataSize = asset->vertexDataSize;
    again.indexData = asset->indexData;
    again.indexDataSize = asset->indexDataSize;
    again.use16BitIndices = asset->use16BitIndices;
    again.vertexLayout = asset->vertexLayout;
    again.bounds = asset->bounds;

    std::vector<u8> second(4096, 0);
    FluxionStream rewriter;
    Fluxion_MemoryStream_InitWriter(&rewriter, second.data(), second.size());
    TEST_CHECK(ctx, Fluxion_MeshAsset_Write(&rewriter, &again));

    TEST_CHECK(ctx, Fluxion_Stream_GetPosition(&rewriter) == written);
    TEST_CHECK(ctx, std::memcmp(bytes.data(), second.data(), written) == 0);

    Fluxion_MeshAsset_Destroy(asset);
}

void RefusesWhatItShould(TestContext* ctx)
{
    const std::vector<Vertex> vertices = { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } } };
    const std::vector<u16> indices = { 0, 0, 0 };

    FluxionRHIVertexLayout layout;
    const FluxionMeshAssetData data = MakeCube(vertices, indices, layout);

    std::vector<u8> bytes(4096, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, bytes.data(), bytes.size());
    TEST_CHECK(ctx, Fluxion_MeshAsset_Write(&writer, &data));
    const usize written = Fluxion_Stream_GetPosition(&writer);

    FluxionMeshAsset* asset = nullptr;

    // Not a mesh at all.
    std::vector<u8> wrongMagic = bytes;
    wrongMagic[0] ^= 0xFFu;
    TEST_CHECK(ctx, !Fluxion_MeshAsset_Read(wrongMagic.data(), written, &asset));

    // Written by a build that knew things this one does not. Refused
    // rather than read as far as it goes: a mesh missing whatever those
    // were would draw, and draw wrongly.
    std::vector<u8> newer = bytes;
    newer[4] = static_cast<u8>(FLUXION_MESH_ASSET_FORMAT_VERSION + 1);
    TEST_CHECK(ctx, !Fluxion_MeshAsset_Read(newer.data(), written, &asset));

    // Cut short. The sizes in the header still say how much there should
    // be, and believing them rather than looking is how a read runs off
    // the end of what it was given.
    TEST_CHECK(ctx, !Fluxion_MeshAsset_Read(bytes.data(), written - 1, &asset));
    TEST_CHECK(ctx, !Fluxion_MeshAsset_Read(bytes.data(), 4, &asset));

    // More attributes than a layout can hold.
    std::vector<u8> tooManyAttributes = bytes;
    tooManyAttributes[8] = FLUXION_RHI_MAX_VERTEX_ATTRIBUTES + 1;
    TEST_CHECK(ctx, !Fluxion_MeshAsset_Read(tooManyAttributes.data(), written, &asset));
}

// Meshes ship in their cooked form, and this module carries no reader for
// any interchange format -- that half belongs to an importer, and an
// importer is not part of what a game runs.
void TheTypeShipsCookedAndClaimsNoSourceFormat(TestContext* ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    const FluxionRHIDeviceHandle noDevice = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIQueueHandle noQueue = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, Fluxion_MeshAsset_RegisterType(noDevice, noQueue));

    const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(Fluxion_MeshAsset_TypeId());
    TEST_CHECK(ctx, type != nullptr);
    if (type)
    {
        TEST_CHECK(ctx, std::strcmp(type->name, FLUXION_MESH_ASSET_TYPE_NAME) == 0);
        TEST_CHECK(ctx, type->defaultShipPolicy == FLUXION_ASSET_SHIP_COOKED);
        TEST_CHECK(ctx, type->sourceExtensionCount == 0);
        TEST_CHECK(ctx, type->import == nullptr);

        // The load half is here, and so is the upload half -- a mesh is
        // exactly the sort of thing that has one.
        TEST_CHECK(ctx, type->load != nullptr);
        TEST_CHECK(ctx, type->finalize != nullptr);
        TEST_CHECK(ctx, type->unload != nullptr);
    }

    Fluxion_MeshAsset_UnregisterType();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Find(Fluxion_MeshAsset_TypeId()) == nullptr);

    Fluxion_AssetTypes_Shutdown();
}

} // namespace

extern "C" void Test_MeshAsset_Run(TestContext* ctx)
{
    std::fprintf(stderr, "  Test_MeshAsset\n");

    RoundTrip(ctx);
    RefusesWhatItShould(ctx);
    TheTypeShipsCookedAndClaimsNoSourceFormat(ctx);
}
