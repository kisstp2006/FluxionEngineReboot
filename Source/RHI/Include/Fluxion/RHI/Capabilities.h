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

#ifdef __cplusplus
extern "C" {
#endif

// A bitmask, not a struct-of-bools -- cheap to copy/compare/intersect
// (`caps & FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES`), and avoids the
// padding a bool-per-field struct would carry. u64 leaves room to grow
// past the current 18 flags without a breaking change.
typedef u64 FluxionRHICapabilityFlags;

#define FLUXION_RHI_CAPABILITY_NONE                    ((FluxionRHICapabilityFlags)0)
#define FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES       ((FluxionRHICapabilityFlags)(1ull << 0))
#define FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING      ((FluxionRHICapabilityFlags)(1ull << 1))
#define FLUXION_RHI_CAPABILITY_MESH_SHADERS             ((FluxionRHICapabilityFlags)(1ull << 2))
#define FLUXION_RHI_CAPABILITY_RAY_TRACING              ((FluxionRHICapabilityFlags)(1ull << 3))
#define FLUXION_RHI_CAPABILITY_RAY_QUERY                ((FluxionRHICapabilityFlags)(1ull << 4))
#define FLUXION_RHI_CAPABILITY_VARIABLE_RATE_SHADING    ((FluxionRHICapabilityFlags)(1ull << 5))
#define FLUXION_RHI_CAPABILITY_ASYNC_COMPUTE            ((FluxionRHICapabilityFlags)(1ull << 6))
#define FLUXION_RHI_CAPABILITY_ASYNC_TRANSFER           ((FluxionRHICapabilityFlags)(1ull << 7))
#define FLUXION_RHI_CAPABILITY_BUFFER_DEVICE_ADDRESS    ((FluxionRHICapabilityFlags)(1ull << 8))
#define FLUXION_RHI_CAPABILITY_INDIRECT_COUNT           ((FluxionRHICapabilityFlags)(1ull << 9))
#define FLUXION_RHI_CAPABILITY_TIMELINE_SYNC            ((FluxionRHICapabilityFlags)(1ull << 10))
#define FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS          ((FluxionRHICapabilityFlags)(1ull << 11))
#define FLUXION_RHI_CAPABILITY_TESSELLATION             ((FluxionRHICapabilityFlags)(1ull << 12))
#define FLUXION_RHI_CAPABILITY_GEOMETRY_SHADERS         ((FluxionRHICapabilityFlags)(1ull << 13))
#define FLUXION_RHI_CAPABILITY_FP16                     ((FluxionRHICapabilityFlags)(1ull << 14))
#define FLUXION_RHI_CAPABILITY_INT64                    ((FluxionRHICapabilityFlags)(1ull << 15))
#define FLUXION_RHI_CAPABILITY_WAVE_OPERATIONS          ((FluxionRHICapabilityFlags)(1ull << 16))
#define FLUXION_RHI_CAPABILITY_SPARSE_RESOURCES         ((FluxionRHICapabilityFlags)(1ull << 17))

// Baseline numeric device limits -- queried once per device, read-only
// after that (same access pattern as FluxionRHIAdapterInfo).
typedef struct FluxionRHILimits
{
    u32 maxTextureDimension1D;
    u32 maxTextureDimension2D;
    u32 maxTextureDimension3D;
    u32 maxTextureArrayLayers;
    u32 maxColorAttachments;
    u32 maxBoundDescriptorSets;
    u32 maxPushConstantSize;
    u32 minUniformBufferOffsetAlignment;
    u32 maxComputeWorkGroupSize[3];
    u32 maxComputeWorkGroupInvocations;
} FluxionRHILimits;

#ifdef __cplusplus
}
#endif
