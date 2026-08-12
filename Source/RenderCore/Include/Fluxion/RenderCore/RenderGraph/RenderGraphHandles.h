#pragma once

#include <Fluxion/Foundation/Handle.h>

// Graph-lifetime handles -- distinct from the underlying
// FluxionRHITextureHandle/FluxionRHIBufferHandle the RHI itself hands out.
// A transient graph resource (declared via
// Fluxion_RenderGraphBuilder_CreateTexture/CreateBuffer) has no RHI handle
// at all until the executor actually creates it for the current frame; an
// imported one (Fluxion_RenderGraph_ImportTexture/ImportBuffer) wraps an
// already-existing RHI handle the graph never creates or destroys. Either
// way, code inside a pass's Setup callback only ever sees these graph
// handles -- the real FluxionRHITextureHandle/FluxionRHIBufferHandle is
// resolved later, during Execute, via Fluxion_RenderGraph_ResolveTexture/
// ResolveBuffer.
FLUXION_DEFINE_HANDLE(FluxionRenderGraphTextureHandle);
FLUXION_DEFINE_HANDLE(FluxionRenderGraphBufferHandle);
FLUXION_DEFINE_HANDLE(FluxionRenderGraphPassHandle);
