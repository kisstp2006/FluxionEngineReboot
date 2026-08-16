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

#include <Fluxion/RHI/NativeHandle.h>
#include <Fluxion/RHI/RHI.h>

// The Null backend has no real GPU objects behind any handle, so every
// native-handle getter is expected to hand back an empty value -- this
// just proves the escape-hatch call path itself doesn't crash and is
// wired up per object type.
void Test_NativeHandle_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));

    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(device));

    FluxionRHINativeHandle nativeDevice = Fluxion_RHI_GetNativeDeviceHandle(device);
    TEST_CHECK(ctx, nativeDevice.value == NULL);

    FluxionRHIBufferDesc bufferDesc = { 64, FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, NULL };
    FluxionRHIBufferHandle buffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);
    FluxionRHINativeHandle nativeBuffer = Fluxion_RHI_GetNativeBufferHandle(buffer);
    TEST_CHECK(ctx, nativeBuffer.value == NULL);

    FluxionRHITextureDesc textureDesc = { 4, 4, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, NULL, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    FluxionRHINativeHandle nativeTexture = Fluxion_RHI_GetNativeTextureHandle(texture);
    TEST_CHECK(ctx, nativeTexture.value == NULL);

    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(buffer);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
