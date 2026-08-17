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

#include <Fluxion/RenderCore/Renderer/MaterialAsset.h>

#include <cstring>

// A material with an identity, checked as bytes.
//
// The half that needs a device -- compiling the shader and handing the
// values to it -- is what the sample exercises every time it starts.
// What is checked here is the half that must be right BEFORE a device
// is involved: that what was written is what comes back, and that
// nothing else is accepted as a material.

namespace
{

const char* const kSource =
    "#include \"Fluxion/Material.jsl\"\n"
    "SurfaceData EvaluateSurface() { return StandardSurface(); }\n";

FluxionMaterialAssetParameter MakeParameter(const char* name, FluxionMaterialAssetParameterKind kind, FluxionVec4 value)
{
    FluxionMaterialAssetParameter parameter{};

    // memcpy rather than a bounded string copy: one compiler here calls
    // every one of those unsafe and treats the warning as an error, and
    // the length is known.
    const usize length = std::strlen(name);
    const usize copied = length < sizeof(parameter.name) - 1 ? length : sizeof(parameter.name) - 1;
    std::memcpy(parameter.name, name, copied);
    parameter.name[copied] = '\0';

    parameter.kind = kind;
    parameter.value = value;
    return parameter;
}

void WhatWasWrittenComesBack(TestContext* ctx)
{
    const FluxionMaterialAssetParameter parameters[] = {
        MakeParameter("baseColorFactor", FLUXION_MATERIAL_ASSET_PARAMETER_VEC4, FluxionVec4{ 0.25f, 0.5f, 0.75f, 1.0f }),
        MakeParameter("roughnessFactor", FLUXION_MATERIAL_ASSET_PARAMETER_FLOAT, FluxionVec4{ 0.4f, 0.0f, 0.0f, 0.0f }),
        MakeParameter("emissiveFactor", FLUXION_MATERIAL_ASSET_PARAMETER_VEC3, FluxionVec4{ 1.0f, 2.0f, 3.0f, 0.0f }),
    };

    FluxionMaterialAssetData data{};
    data.source = kSource;
    data.parameters = parameters;
    data.parameterCount = 3;

    u8 cooked[4096];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, Fluxion_MaterialAsset_Write(&writer, &data));
    TEST_CHECK(ctx, !Fluxion_Stream_HasOverflowed(&writer));
    const usize cookedSize = Fluxion_Stream_GetPosition(&writer);

    FluxionMaterialAsset* asset = nullptr;
    TEST_CHECK(ctx, Fluxion_MaterialAsset_Read(cooked, cookedSize, &asset));
    if (asset == nullptr)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    // The source, character for character: it is what the shader is
    // compiled from, and a copy that lost its last line would fail much
    // later and much less clearly.
    TEST_CHECK(ctx, asset->source != nullptr && std::strcmp(asset->source, kSource) == 0);

    TEST_CHECK(ctx, asset->parameterCount == 3);
    TEST_CHECK(ctx, std::strcmp(asset->parameters[0].name, "baseColorFactor") == 0);
    TEST_CHECK(ctx, asset->parameters[0].kind == FLUXION_MATERIAL_ASSET_PARAMETER_VEC4);
    TEST_CHECK(ctx, asset->parameters[0].value.z == 0.75f);
    TEST_CHECK(ctx, asset->parameters[1].value.x == 0.4f);
    TEST_CHECK(ctx, asset->parameters[2].kind == FLUXION_MATERIAL_ASSET_PARAMETER_VEC3);
    TEST_CHECK(ctx, asset->parameters[2].value.y == 2.0f);

    // Nothing has been given to a device, and that is said rather than
    // left as a zeroed handle -- slot zero of either pool is a real
    // object somebody else is using.
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->material));
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->pipeline));
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->program));

    Fluxion_MaterialAsset_Destroy(asset);
}

void NothingElseIsAMaterial(TestContext* ctx)
{
    FluxionMaterialAsset* refused = nullptr;

    static const u8 notAMaterial[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    TEST_CHECK(ctx, !Fluxion_MaterialAsset_Read(notAMaterial, sizeof(notAMaterial), &refused));

    // A material with no source is not a material: there would be
    // nothing to compile, and the failure would arrive at a device.
    FluxionMaterialAssetData empty{};
    u8 cooked[256];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, !Fluxion_MaterialAsset_Write(&writer, &empty));

    // And one whose bytes stop halfway.
    FluxionMaterialAssetData data{};
    data.source = kSource;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, Fluxion_MaterialAsset_Write(&writer, &data));
    TEST_CHECK(ctx, !Fluxion_MaterialAsset_Read(cooked, Fluxion_Stream_GetPosition(&writer) / 2, &refused));
    TEST_CHECK(ctx, refused == nullptr);
}

} // namespace

extern "C" void Test_MaterialAsset_Run(TestContext* ctx)
{
    WhatWasWrittenComesBack(ctx);
    NothingElseIsAMaterial(ctx);
}
