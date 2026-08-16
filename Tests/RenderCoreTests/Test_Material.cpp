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

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstdio>

namespace
{

struct NullRHIFixture
{
    FluxionRHIInstanceHandle instance;
    FluxionRHIDeviceHandle device;
};

NullRHIFixture CreateNullRHIFixture()
{
    NullRHIFixture fixture;
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    fixture.instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(fixture.instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    return fixture;
}

void DestroyNullRHIFixture(const NullRHIFixture& fixture)
{
    Fluxion_RHI_DestroyDevice(fixture.device);
    Fluxion_RHI_DestroyInstance(fixture.instance);
}

const char* kVertexSource =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

// Reflects one MATERIAL-group uniform (tint, Vector3) and no textures --
// enough to exercise SetVec3/SetFloat's name+kind matching without
// needing a real texture/sampler round trip too.
const char* kFragmentSource =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, 1.0);\n"
    "}\n";

} // namespace

extern "C" void Test_Material_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping Material checks\n");
        return;
    }

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionShaderProgramDesc programDesc = {};
    programDesc.debugName = "Test_Material.Program";
    programDesc.vertexSource = kVertexSource;
    programDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(fixture.device, &programDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

    FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));

    // "tint" is a Vector3 (Material group) -- SetVec3 must succeed,
    // SetFloat/SetVec4 on the same name must not (wrong kind), and any
    // name the shader never declared must fail regardless of kind.
    TEST_CHECK(ctx, Fluxion_Material_SetVec3(material, "tint", FluxionVec3{ 1.0f, 0.5f, 0.25f }));
    TEST_CHECK(ctx, !Fluxion_Material_SetFloat(material, "tint", 1.0f));
    TEST_CHECK(ctx, !Fluxion_Material_SetVec4(material, "tint", FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f }));
    TEST_CHECK(ctx, !Fluxion_Material_SetVec3(material, "doesNotExist", FluxionVec3{ 0.0f, 0.0f, 0.0f }));

    FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, !Fluxion_Material_SetTexture(material, "tint", noView, noSampler)); // "tint" is a uniform, not a texture

    Fluxion_Material_FlushDirty(material);
    Fluxion_Material_FlushDirty(material); // a second, no-op flush must not assert or crash

    Fluxion_Material_Destroy(material);
    Fluxion_ShaderProgram_Destroy(program);

    DestroyNullRHIFixture(fixture);
}
