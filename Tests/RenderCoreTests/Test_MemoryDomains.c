#include "TestFramework.h"

#include <Fluxion/Foundation/Memory/MemoryTracker.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

// The renderer's memory domains only mean something if the numbers move
// when work happens and settle when it is undone. A mesh upload is the
// smallest complete story: staging bytes appear under GPUUpload and are
// gone by the time Create returns; the vertex/index bytes appear under
// Renderer and stay until the mesh is destroyed; and because GPUUpload is
// a child, its traffic also rolls up into the Renderer total.
void Test_MemoryDomains_Run(TestContext* ctx)
{
#if FLUXION_MEMORY_TRACKING
    Fluxion_MemoryTracker_Init();

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests.MemoryDomains", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);
    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    typedef struct Vertex { f32 position[3]; } Vertex;
    Vertex vertices[3] = { { { 0, 0, 0 } }, { { 1, 0, 0 } }, { { 0, 1, 0 } } };
    u16 indices[3] = { 0, 1, 2 };

    FluxionMeshBufferDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexData = vertices;
    desc.vertexDataSize = sizeof(vertices);
    desc.indexData = indices;
    desc.indexDataSize = sizeof(indices);
    desc.use16BitIndices = true;
    desc.vertexLayout.attributeCount = 1;
    desc.vertexLayout.attributes[0].location = 0;
    desc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    desc.vertexLayout.stride = sizeof(Vertex);
    desc.bounds.min = (FluxionVec3){ 0, 0, 0 };
    desc.bounds.max = (FluxionVec3){ 1, 1, 0 };
    desc.debugName = "Test_MemoryDomains.Mesh";

    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    FluxionMemoryStatistics rendererStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(Renderer));
    FluxionMemoryStatistics uploadStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(GPUUpload));

    // The mesh's own GPU bytes are standing right now.
    TEST_CHECK(ctx, rendererStats.currentBytes >= sizeof(vertices) + sizeof(indices));

    // The staging traffic came and went inside Create -- an upload that
    // never happened and one that leaked would both fail here, from
    // opposite directions.
    TEST_CHECK(ctx, uploadStats.allocationCount == 1);
    TEST_CHECK(ctx, uploadStats.deallocationCount == 1);
    TEST_CHECK(ctx, uploadStats.currentBytes == 0);
    TEST_CHECK(ctx, uploadStats.peakBytes >= sizeof(vertices) + sizeof(indices));

    // Roll-up: the parent saw the upload traffic too. Its peak covers
    // the moment mesh bytes and staging bytes stood at once.
    TEST_CHECK(ctx, rendererStats.peakBytes >= uploadStats.peakBytes);

    Fluxion_MeshBuffer_Destroy(mesh);
    rendererStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(Renderer));
    TEST_CHECK(ctx, rendererStats.currentBytes == 0);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
    Fluxion_MemoryTracker_Shutdown();
#else
    FLUXION_UNUSED(ctx);
#endif
}
