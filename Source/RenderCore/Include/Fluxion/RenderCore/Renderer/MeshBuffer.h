#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionAABB
{
    FluxionVec3 min;
    FluxionVec3 max;
} FluxionAABB;

FLUXION_DEFINE_HANDLE(FluxionMeshBufferHandle);

typedef struct FluxionMeshBufferDesc
{
    const void* vertexData;
    usize vertexDataSize;
    const void* indexData;
    usize indexDataSize;
    // RHI.h has no standalone index-format enum to reuse --
    // Fluxion_RHI_CommandList_SetIndexBuffer itself takes a plain
    // use16BitIndices bool, so this mirrors that instead of inventing a
    // format enum the RHI doesn't have.
    bool use16BitIndices;
    FluxionRHIVertexLayout vertexLayout;
    FluxionAABB bounds;
    const char* debugName; // optional, may be NULL
} FluxionMeshBufferDesc;

// Uploads vertexData/indexData into GPU_ONLY buffers through a CPU
// staging buffer, a queue submission, and a fence wait (the same
// staging pattern Samples/ForwardRendererDemo's manual upload code
// uses) -- synchronous: blocks until the upload completes, so the
// staging buffer can be torn down before returning.
FluxionMeshBufferHandle Fluxion_MeshBuffer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, const FluxionMeshBufferDesc* desc);
void Fluxion_MeshBuffer_Destroy(FluxionMeshBufferHandle mesh);

#ifdef __cplusplus
}
#endif
