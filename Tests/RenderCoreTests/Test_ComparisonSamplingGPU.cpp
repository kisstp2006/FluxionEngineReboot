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

#include <Fluxion/Foundation/Handle.hpp>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstring>

// The comparison sample, put in front of every real shader compiler
// this engine ships with.
//
// The text each backend receives is checked elsewhere, by reading it.
// This checks the thing reading it cannot: that DXC and a real OpenGL
// driver ACCEPT what comes out. Those are different questions, and the
// gap between them is exactly where a sampling call that existed on one
// backend only once got through -- a shader compiled on OpenGL, was
// never handed to DXC, and the missing half surfaced far downstream.
//
// A shadow map bound beside a comparing sampler is also the first depth
// texture in this engine that is drawn into AND read from. Nothing else
// has ever asked a backend for both at once.

namespace
{

// A vertex stage writes its clip position to the output named Position
// rather than returning it -- the same shape the engine's own vertex
// passes use.
const char* const kVertexSource =
    "[Input] Vector3 position;\n"
    "[Output] Vector3 shadowCoord;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  shadowCoord = position;\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

const char* const kFragmentSource =
    "[Texture(Material)] Texture2DShadow shadowMap;\n"
    "[Input] Vector3 shadowCoord;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  float lit = textureCompare(shadowMap, shadowCoord.xy, shadowCoord.z);\n"
    "  return Vector4(lit, lit, lit, 1.0);\n"
    "}\n";

void CheckOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s is not in this build -- comparison sampling was NOT checked on it.", backendName);
        return;
    }

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No usable %s loader -- comparison sampling was NOT checked on it.", backendName);
        return;
    }

    FluxionRHIAdapterHandle adapters[8];
    FluxionRHIDeviceHandle device = Fluxion::Foundation::NoHandle<FluxionRHIDeviceHandle>();
    if (Fluxion_RHI_EnumerateAdapters(instance, adapters, 8) != 0)
    {
        FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
        device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    }

    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- comparison sampling was NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    // The whole check: this backend's own compiler is handed the
    // generated text and has to accept it. A failure here is logged by
    // ShaderProgram with the compiler's own message, so a broken
    // emission says what is wrong with it rather than only that
    // something is.
    FluxionShaderProgramDesc programDesc;
    std::memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "ComparisonSampling.Shadow";
    programDesc.vertexSource = kVertexSource;
    programDesc.fragmentSource = kFragmentSource;

    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));
    if (FLUXION_HANDLE_IS_VALID(program)) Fluxion_ShaderProgram_Destroy(program);

    // And a depth texture that is BOTH drawn into and sampled, with a
    // comparing sampler beside it. Two things this engine has never
    // asked a backend for, made here so that a backend which refuses
    // either says so now rather than under the first shadow pass.
    FluxionRHITextureDesc depthDesc;
    std::memset(&depthDesc, 0, sizeof(depthDesc));
    depthDesc.width = 64;
    depthDesc.height = 64;
    depthDesc.depth = 1;
    depthDesc.mipLevels = 1;
    depthDesc.arrayLayers = 1;
    depthDesc.sampleCount = 1;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    depthDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    depthDesc.debugName = "ComparisonSampling.ShadowMap";

    FluxionRHITextureHandle shadowMap = Fluxion_RHI_CreateTexture(device, &depthDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(shadowMap));

    FluxionRHITextureViewDesc viewDesc;
    std::memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = shadowMap;
    viewDesc.format = depthDesc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle shadowView = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(shadowView));

    FluxionRHISamplerDesc samplerDesc;
    std::memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "ComparisonSampling.CompareSampler";
    samplerDesc.compareEnable = true;
    samplerDesc.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;

    FluxionRHISamplerHandle compareSampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(compareSampler));

    // Bound together, which is where D3D12 actually builds its sampler:
    // the desc above is only stored until a bind group asks for it, so
    // a comparison filter it refused would surface here and nowhere
    // earlier.
    FluxionRHIBindGroupLayoutDesc layoutDesc = { };
    layoutDesc.entries[0].binding = 0;
    layoutDesc.entries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    layoutDesc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    layoutDesc.entries[1].binding = 1;
    layoutDesc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    layoutDesc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    layoutDesc.entryCount = 2;
    layoutDesc.debugName = "ComparisonSampling.Layout";

    FluxionRHIBindGroupLayoutHandle layout = Fluxion_RHI_CreateBindGroupLayout(device, &layoutDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(layout));

    if (FLUXION_HANDLE_IS_VALID(layout))
    {
        const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
        const FluxionRHITextureViewHandle noView = Fluxion::Foundation::NoHandle<FluxionRHITextureViewHandle>();
        const FluxionRHISamplerHandle noSampler = Fluxion::Foundation::NoHandle<FluxionRHISamplerHandle>();

        FluxionRHIBindGroupEntry entries[2];
        std::memset(entries, 0, sizeof(entries));
        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[0].buffer = noBuffer;
        entries[0].textureView = shadowView;
        entries[0].sampler = noSampler;
        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[1].buffer = noBuffer;
        entries[1].textureView = noView;
        entries[1].sampler = compareSampler;

        FluxionRHIBindGroupDesc groupDesc;
        groupDesc.layout = layout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 2;

        FluxionRHIBindGroupHandle group = Fluxion_RHI_CreateBindGroup(device, &groupDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(group));
        if (FLUXION_HANDLE_IS_VALID(group)) Fluxion_RHI_DestroyBindGroup(group);

        Fluxion_RHI_DestroyBindGroupLayout(layout);
    }

    if (FLUXION_HANDLE_IS_VALID(compareSampler)) Fluxion_RHI_DestroySampler(compareSampler);
    if (FLUXION_HANDLE_IS_VALID(shadowView)) Fluxion_RHI_DestroyTextureView(shadowView);
    if (FLUXION_HANDLE_IS_VALID(shadowMap)) Fluxion_RHI_DestroyTexture(shadowMap);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_ComparisonSamplingGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- comparison sampling was NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
