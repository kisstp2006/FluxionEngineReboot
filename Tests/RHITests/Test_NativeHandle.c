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

    FluxionRHITextureDesc textureDesc = { 4, 4, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, NULL };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    FluxionRHINativeHandle nativeTexture = Fluxion_RHI_GetNativeTextureHandle(texture);
    TEST_CHECK(ctx, nativeTexture.value == NULL);

    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(buffer);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
