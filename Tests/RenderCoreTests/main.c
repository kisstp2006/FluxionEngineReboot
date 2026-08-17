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

#include <Fluxion/Foundation/Log.h>

void Test_PassRegistry_Run(TestContext* ctx);
void Test_GraphCompile_Run(TestContext* ctx);
void Test_JsonLoader_Run(TestContext* ctx);
void Test_DumpDot_Run(TestContext* ctx);
void Test_RenderGraphAsset_Run(TestContext* ctx);
void Test_RenderPipelineAsset_Run(TestContext* ctx);
void Test_RenderPipelineAssetGPU_Run(TestContext* ctx);
void Test_GPUScene_Run(TestContext* ctx);
void Test_MaterialAsset_Run(TestContext* ctx);
void Test_ShaderProgram_Run(TestContext* ctx);
void Test_ShaderReload_Run(TestContext* ctx);
void Test_Material_Run(TestContext* ctx);
void Test_MeshBuffer_Run(TestContext* ctx);
void Test_MemoryDomains_Run(TestContext* ctx);
void Test_RenderView_Run(TestContext* ctx);
void Test_ShadowAtlas_Run(TestContext* ctx);
void Test_ShadowMatrices_Run(TestContext* ctx);
void Test_ComparisonSamplingGPU_Run(TestContext* ctx);
void Test_ShadowPassGPU_Run(TestContext* ctx);
void Test_RendererFrame_Run(TestContext* ctx);
void Test_MeshAsset_Run(TestContext* ctx);
void Test_ShaderLibrary_Run(TestContext* ctx);
void Test_SurfaceData_Run(TestContext* ctx);
void Test_MaterialParameters_Run(TestContext* ctx);
void Test_TextureAsset_Run(TestContext* ctx);
void Test_BlockCompressGPU_Run(TestContext* ctx);
void Test_LightingGPU_Run(TestContext* ctx);
void Test_Exposure_Run(TestContext* ctx);
void Test_StandardMaterialCompiles_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RenderCoreTests", "Running RenderCoreTests...");

    Test_PassRegistry_Run(&ctx);
    Test_GraphCompile_Run(&ctx);
    Test_JsonLoader_Run(&ctx);
    Test_DumpDot_Run(&ctx);
    Test_RenderGraphAsset_Run(&ctx);
    Test_RenderPipelineAsset_Run(&ctx);
    Test_RenderPipelineAssetGPU_Run(&ctx);
    Test_GPUScene_Run(&ctx);
    Test_MaterialAsset_Run(&ctx);
    Test_ShaderProgram_Run(&ctx);
    Test_ShaderReload_Run(&ctx);
    Test_Material_Run(&ctx);
    Test_MeshBuffer_Run(&ctx);
    Test_MemoryDomains_Run(&ctx);
    Test_RenderView_Run(&ctx);
    Test_ShadowAtlas_Run(&ctx);
    Test_ShadowMatrices_Run(&ctx);
    Test_ComparisonSamplingGPU_Run(&ctx);
    Test_ShadowPassGPU_Run(&ctx);
    Test_RendererFrame_Run(&ctx);
    Test_MeshAsset_Run(&ctx);
    Test_ShaderLibrary_Run(&ctx);
    Test_SurfaceData_Run(&ctx);
    Test_MaterialParameters_Run(&ctx);
    Test_TextureAsset_Run(&ctx);
    Test_BlockCompressGPU_Run(&ctx);
    Test_LightingGPU_Run(&ctx);
    Test_Exposure_Run(&ctx);
    Test_StandardMaterialCompiles_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RenderCoreTests", "All RenderCoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RenderCoreTests", "%d RenderCoreTests check(s) failed.", ctx.failures);
    return 1;
}
