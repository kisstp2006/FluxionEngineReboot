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

#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphHandles.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque -- the real definition lives in Private/RenderGraph/ and is only
// ever handed to a pass's own Setup callback. A pass type never allocates
// or stores one itself; it just calls the Fluxion_RenderGraphBuilder_*
// functions below with the pointer it was given.
typedef struct FluxionRenderGraphBuilder FluxionRenderGraphBuilder;

// Called once per node instance during the graph's Setup phase (i.e. from
// inside Fluxion_RenderGraph_Compile, not at the point the node was added
// to the graph). The pass declares -- via the Fluxion_RenderGraphBuilder_*
// calls below -- every resource it reads/writes/creates. It must NOT issue
// any RHI call here: no command list exists yet, and no resource has been
// created yet either (transient resources are only allocated later, during
// Execute).
typedef void (*FluxionRenderGraphPassSetupFn)(FluxionRenderGraphBuilder* builder, void* userData);

// Called once per node instance during Execute, in the compiled
// (dependency-respecting) order, with the real, resolved RHI handles for
// every resource this pass declared in Setup available via
// Fluxion_RenderGraph_ResolveTexture/ResolveBuffer -- this is where actual
// Fluxion_RHI_CommandList_* calls belong. `commandList` is the single
// command list the whole graph records into; it is already in the
// recording state (the graph never calls Begin/End itself -- the caller,
// which owns the command list, does).
typedef void (*FluxionRenderGraphPassExecuteFn)(FluxionRHICommandListHandle commandList, void* userData);

typedef struct FluxionRenderGraphPassType
{
    const char* name; // registry key, e.g. "CubeBrightnessCompute"
    FluxionRenderGraphPassSetupFn setup;
    FluxionRenderGraphPassExecuteFn execute;
} FluxionRenderGraphPassType;

// --- Builder calls, valid only from inside a Setup callback ----------------
//
// `resourceName` is how passes and JSON-described nodes refer to the same
// logical resource by string (the "pin" identity) -- the builder resolves
// it against every resource name already known to the graph (created or
// imported by any node, regardless of node order -- a Read/Write may name
// a resource a later-added node creates; the dependency is derived from
// what creates/reads/writes what, not from the order nodes were added) or
// registers a brand-new one for Create*. A Read/Write naming a resource
// nothing ever creates or imports is a hard error -- surfaced by
// Fluxion_RenderGraph_Compile returning false, not a silent no-op.
//
// The handles returned here are placeholders valid only for this same
// pass instance's later Execute call, to look up its own resolved RHI
// handle via Fluxion_RenderGraph_ResolveTexture/ResolveBuffer.

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_CreateTexture(FluxionRenderGraphBuilder* builder, const char* resourceName, const FluxionRHITextureDesc* desc);
FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_CreateBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName, const FluxionRHIBufferDesc* desc);

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_ReadTexture(FluxionRenderGraphBuilder* builder, const char* resourceName);
FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteTexture(FluxionRenderGraphBuilder* builder, const char* resourceName);

// Same as WriteTexture, but declares the more specific "written as a
// color/depth attachment" usage (FLUXION_RENDER_GRAPH_USAGE_WRITE_COLOR_TARGET/
// WRITE_DEPTH_TARGET) so the compiler generates a RENDER_TARGET/DEPTH_WRITE
// barrier instead of plain WRITE's SHADER_WRITE -- use these for a pass
// that binds the texture via FluxionRHIRenderingDesc, not
// Fluxion_RHI_CommandList_Dispatch's storage-image bindings.
FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteColorTarget(FluxionRenderGraphBuilder* builder, const char* resourceName);
FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteDepthTarget(FluxionRenderGraphBuilder* builder, const char* resourceName);

FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_ReadBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName);
FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_WriteBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName);

#ifdef __cplusplus
}
#endif
