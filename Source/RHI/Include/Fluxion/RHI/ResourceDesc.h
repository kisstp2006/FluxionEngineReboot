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
#include <Fluxion/RHI/Format.h>

#ifdef __cplusplus
extern "C" {
#endif

// Which physical memory a resource should live in -- the backend maps
// this onto its own allocator (e.g. VMA usage flags for Vulkan). Kept
// separate from FluxionRHIResourceState: state is "what a resource is
// being used for right now", memory class is "where its bytes live" and
// doesn't change over the resource's lifetime.
typedef enum FluxionRHIMemoryClass
{
    FLUXION_RHI_MEMORY_CLASS_GPU_ONLY = 0,
    FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU,
    FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU,
    FLUXION_RHI_MEMORY_CLASS_TRANSIENT,
    FLUXION_RHI_MEMORY_CLASS_READBACK,
} FluxionRHIMemoryClass;

#define FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER  ((u32)(1u << 0))
#define FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER   ((u32)(1u << 1))
#define FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER ((u32)(1u << 2))
#define FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER ((u32)(1u << 3))
#define FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC   ((u32)(1u << 4))
#define FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST   ((u32)(1u << 5))
#define FLUXION_RHI_BUFFER_USAGE_INDIRECT       ((u32)(1u << 6))

typedef struct FluxionRHIBufferDesc
{
    usize size;
    u32 usageFlags; // FLUXION_RHI_BUFFER_USAGE_*
    FluxionRHIMemoryClass memoryClass;
    const char* debugName; // optional, may be NULL
} FluxionRHIBufferDesc;

#define FLUXION_RHI_TEXTURE_USAGE_SAMPLED       ((u32)(1u << 0))
#define FLUXION_RHI_TEXTURE_USAGE_STORAGE       ((u32)(1u << 1))
#define FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET ((u32)(1u << 2))
#define FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL ((u32)(1u << 3))
#define FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC  ((u32)(1u << 4))
#define FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST  ((u32)(1u << 5))

// What shape a texture is, which is not the same question as how big.
//
// A cube map is six square layers that a shader samples by DIRECTION
// rather than by coordinate, and that is a property of the texture, not
// of how many layers it happens to have: six layers of a wall atlas are
// not a cube. So it is said outright.
typedef enum FluxionRHITextureDimension
{
    // Zero, so every description written before this existed still means
    // what it meant.
    FLUXION_RHI_TEXTURE_DIMENSION_2D = 0,

    // Exactly six array layers, in the order every one of these backends
    // agrees on: +X, -X, +Y, -Y, +Z, -Z. One order, written down once,
    // because a face order that differed per backend would give a sky
    // that is merely rotated -- which looks like an artist's mistake
    // rather than a bug.
    FLUXION_RHI_TEXTURE_DIMENSION_CUBE,
} FluxionRHITextureDimension;

#define FLUXION_RHI_CUBE_FACE_COUNT 6

typedef struct FluxionRHITextureDesc
{
    u32 width;
    u32 height;
    u32 depth;        // 1 for a 2D texture
    u32 mipLevels;     // >= 1
    u32 arrayLayers;   // >= 1
    u32 sampleCount;   // 1 = no multisampling
    FluxionRHIFormat format;
    u32 usageFlags;    // FLUXION_RHI_TEXTURE_USAGE_*
    FluxionRHIMemoryClass memoryClass;
    const char* debugName; // optional, may be NULL

    // Added at the end on purpose: several callers build one of these
    // with a positional initializer, and a field in the middle would
    // silently shift every value after it.
    //
    // A CUBE description must have six array layers and equal width and
    // height. Anything else is refused rather than reinterpreted.
    FluxionRHITextureDimension dimension;
} FluxionRHITextureDesc;

typedef enum FluxionRHIFilter
{
    FLUXION_RHI_FILTER_NEAREST = 0,
    FLUXION_RHI_FILTER_LINEAR,
} FluxionRHIFilter;

typedef enum FluxionRHIAddressMode
{
    FLUXION_RHI_ADDRESS_MODE_REPEAT = 0,
    FLUXION_RHI_ADDRESS_MODE_MIRRORED_REPEAT,
    FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE,
    FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_BORDER,
} FluxionRHIAddressMode;

// Which way a comparison has to come out to pass.
//
// Declared HERE rather than beside the depth state that also uses it,
// and for a mechanical reason: RHI.h includes this file, so a type
// named in both has to be in the one that comes first. Two of them --
// the depth test and a shadow sampler -- ask the same question, so
// there is one enum for it.
typedef enum FluxionRHICompareOp
{
    FLUXION_RHI_COMPARE_OP_NEVER = 0,
    FLUXION_RHI_COMPARE_OP_LESS,
    FLUXION_RHI_COMPARE_OP_EQUAL,
    FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL,
    FLUXION_RHI_COMPARE_OP_GREATER,
    FLUXION_RHI_COMPARE_OP_NOT_EQUAL,
    FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL,
    FLUXION_RHI_COMPARE_OP_ALWAYS,
} FluxionRHICompareOp;

typedef struct FluxionRHISamplerDesc
{
    FluxionRHIFilter minFilter;
    FluxionRHIFilter magFilter;
    FluxionRHIFilter mipFilter;
    FluxionRHIAddressMode addressModeU;
    FluxionRHIAddressMode addressModeV;
    FluxionRHIAddressMode addressModeW;
    f32 maxAnisotropy; // 1.0 = disabled
    const char* debugName; // optional, may be NULL

    // A shadow sampler: the hardware compares each texel against a
    // reference the shader supplies and FILTERS THE ANSWERS, rather than
    // returning the stored depths for the shader to compare afterwards.
    // The difference is the whole point -- an average of two depths means
    // nothing, while an average of two yes-or-no answers is a soft edge.
    //
    // Only meaningful for a sampler bound beside a Texture2DShadow. At
    // the end, and off by default, so every positional initializer
    // written before this existed still says what it said.
    bool compareEnable;
    FluxionRHICompareOp compareOp;
} FluxionRHISamplerDesc;

#ifdef __cplusplus
}
#endif
