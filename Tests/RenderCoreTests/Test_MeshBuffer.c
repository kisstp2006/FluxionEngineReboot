#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

typedef struct Test_MeshBuffer_Vertex
{
    float position[3];
} Test_MeshBuffer_Vertex;

void Test_MeshBuffer_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    Test_MeshBuffer_Vertex vertices[3] = {
        { { 0.0f, 0.5f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f } },
    };
    unsigned short indices[3] = { 0, 1, 2 };

    FluxionRHIVertexLayout vertexLayout;
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = sizeof(Test_MeshBuffer_Vertex);

    FluxionMeshBufferDesc desc;
    desc.vertexData = vertices;
    desc.vertexDataSize = sizeof(vertices);
    desc.indexData = indices;
    desc.indexDataSize = sizeof(indices);
    desc.use16BitIndices = true;
    desc.vertexLayout = vertexLayout;
    desc.bounds.min = (FluxionVec3){ -0.5f, -0.5f, 0.0f };
    desc.bounds.max = (FluxionVec3){ 0.5f, 0.5f, 0.0f };
    desc.debugName = "Test_MeshBuffer.Triangle";

    FluxionMeshBufferHandle indexedMesh = Fluxion_MeshBuffer_Create(device, queue, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(indexedMesh));

    FluxionMeshBufferDesc nonIndexedDesc = desc;
    nonIndexedDesc.indexData = NULL;
    nonIndexedDesc.indexDataSize = 0;
    FluxionMeshBufferHandle nonIndexedMesh = Fluxion_MeshBuffer_Create(device, queue, &nonIndexedDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(nonIndexedMesh));

    Fluxion_MeshBuffer_Destroy(indexedMesh);
    Fluxion_MeshBuffer_Destroy(nonIndexedMesh);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
