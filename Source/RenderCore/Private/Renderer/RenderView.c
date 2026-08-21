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

#include <Fluxion/RenderCore/Renderer/RenderView.h>

#include "RendererInternal.h"

#include <Fluxion/RenderCore/Renderer/TextureDefaults.h>


#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

typedef struct FluxionRenderViewRecord
{
    bool alive;
    u32 generation;

    FluxionRHIDeviceHandle device;

    FluxionMat4 viewMatrix;
    FluxionMat4 projectionMatrix;

    // The one the frame before this used, and whether anybody said so.
    // Nobody saying means "the same as this frame", which reads as
    // nothing having moved.
    FluxionMat4 previousViewProjection;
    bool hasPreviousViewProjection;
    FluxionViewport viewport;
    FluxionScissorRect scissor;
    FluxionRenderTargetHandle renderTarget;
    u32 layerMask;

    FluxionVec3 ambientColor;
    f32 cullDistance;
    f32 exposure;
    f32 tonemapWhitePoint;

    // WHETHER SOMETHING ELSE WILL DO IT. The camera and the curve belong
    // to the frame either way -- what changes is where they are applied:
    // in the shader that draws each surface, or once at the end, by the
    // pass that turns a target full of light into a picture. The values
    // stay here in both cases, because the pass that applies them asks
    // this view for them.
    bool toneMappingDeferred;

    // What glows: the brightness it takes, how softly that begins, and
    // how much of the result is added back.
    f32 bloomThreshold;
    f32 bloomKnee;
    f32 bloomIntensity;
    bool encodeOutputToSRGB;

    // What the world looks like in every direction. Never nothing: a
    // view starts with the small black cube the engine keeps.
    FluxionRHITextureViewHandle environmentView;
    FluxionRHISamplerHandle environmentSampler;
    f32 environmentIntensity;

    // How much of the sky reaches each pixel, worked out by a pass this
    // view knows nothing about -- so the renderer hands it over, and a
    // view that was never handed one keeps the white stand-in that says
    // "all of it".
    FluxionRHITextureViewHandle occlusionView;
    bool occlusionMeasured;

    // What the grading was asked for, as distances from leaving the
    // picture alone -- see the description's own comment for why they are
    // stored that way rather than as the numbers the shader multiplies
    // by.
    // What the measured brightness is allowed to do to the camera, and
    // how long the last frame took -- see the description's own comments.
    f32 occlusionRadius;
    f32 occlusionStrength;
    u32 occlusionSliceCount;
    u32 occlusionStepCount;

    f32 autoExposureKey;
    f32 autoExposureSpeed;
    f32 autoExposureLowest;
    f32 autoExposureHighest;
    f32 deltaSeconds;

    f32 gradeTemperature;
    f32 gradeTint;
    f32 gradeContrast;
    f32 gradeSaturation;
    FluxionVec3 gradeLift;
    FluxionVec3 gradeGamma;
    FluxionVec3 gradeGain;

    // The one this view made for itself, kept apart from the one it is
    // currently using: a caller may set an environment with its own
    // sampler, and this still has to be given back.
    FluxionRHISamplerHandle ownedEnvironmentSampler;

    // The sky as nine coefficients per channel, written by a compute pass
    // and read by every surface. Never read back: what produces it and
    // what consumes it are both on the device.
    FluxionRHIBufferHandle irradianceBuffer;

    // The environment pre-blurred per roughness, and the split-sum table
    // beside it. Owned like the buffer above: the renderer's passes fill
    // them, every surface reads them, nothing crosses back to the CPU.
    FluxionRHITextureHandle prefilteredTexture;
    FluxionRHITextureViewHandle prefilteredView;
    FluxionRHITextureHandle dfgTexture;
    FluxionRHITextureViewHandle dfgView;

    // Cleared once the table's fill has been recorded. The table depends
    // on no sky, so once is all there is.
    bool dfgWanted;

    // Whether the prefiltered chain has been filled at least once. The
    // refill's barriers need the texture's REAL previous state: claiming
    // "undefined" over a texture the last frame sampled is a lie one
    // backend forgives and another answers with garbage.
    bool prefilteredFilled;

    // Every shadow in this frame, in one texture cut into tiles. Per
    // view rather than per renderer because what a shadow has to cover
    // depends on where this view is looking from.
    FluxionRHITextureHandle shadowAtlasTexture;
    FluxionRHITextureViewHandle shadowAtlasView;

    // The size the atlas was actually made at, and the tile it is cut
    // into. Kept rather than read from a constant: two views of one
    // scene may be drawn through pipelines that asked for different
    // shadow quality, and the texture each got is the answer for it.
    u32 shadowAtlasSize;
    u32 shadowTileSize;

    // What the shadow pass draws this frame: a matrix per shadow and the
    // tile each was given. Kept here as well as in the buffer below
    // because the pass needs them untransposed and the shader does not
    // need them at all until something is lit.
    FluxionMat4 shadowMatrices[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    FluxionShadowAtlasTile shadowTiles[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    u32 shadowCount;

    // The same shadows in the shape a shader reads, and the pair of
    // buffers that get them there -- for the same reason the lights need
    // a pair, see MakeLightStorage.
    FluxionRHIBufferHandle shadowStaging;
    FluxionRHIBufferHandle shadowStorage;

    // Turns a read of the atlas into an answer rather than a depth. Made
    // with the view and destroyed with it, like the environment's own.
    FluxionRHISamplerHandle shadowSampler;

    // Whether the atlas has been given a value yet. It is bound to every
    // draw whether or not anything casts a shadow -- a shader cannot ask
    // whether a texture is there -- so a view that never ran a shadow
    // pass would otherwise have every surface reading a texture nothing
    // had written, which one backend calls out and another answers with
    // whatever was in the memory.
    bool shadowAtlasPrepared;

    // Set when the environment changes, cleared once the projection has
    // been recorded. Without it the sky would be projected again on every
    // frame, for an answer that cannot have moved.
    bool environmentDirty;

    // The lights, in the packed shape the shader reads, plus the two
    // buffers that get them there: one the CPU writes, one the GPU reads.
    FluxionRHIBufferHandle lightStaging;
    FluxionRHIBufferHandle lightStorage;
    u32 lightCapacity; // in lights, never shrinks
    u32 lightCount;

    FluxionRHIBufferHandle frameConstantBuffer; // uniform buffer holding this frame's FluxionFrameConstants
    FluxionRHIBindGroupLayoutHandle frameBindGroupLayout;
    FluxionRHIBindGroupHandle frameBindGroup;
} FluxionRenderViewRecord;

// What one light looks like to a shader.
//
// Four vectors of four, and every field packed into them by hand, because
// the two shading languages this compiles to do not lay out a structure
// the same way when it mixes three-component vectors with single floats.
// Four-component vectors are the one shape both agree on without a rule
// anybody has to remember -- and a layout the two ends disagreed about
// would not fail, it would read the right memory in the wrong pieces.
//
// The spare components are not waste worth removing: sixty-four bytes a
// light is nothing beside being sure the two sides agree.
typedef struct FluxionRenderLightGPU
{
    f32 positionRange[4]; // xyz world position, w range
    f32 direction[4];     // xyz the way the light travels, w which kind it is
    f32 color[4];         // rgb colour-and-intensity, w cos(inner cone)
    f32 cone[4];          // x cos(outer cone), yzw spare
} FluxionRenderLightGPU;

// What one shadow looks like to a shader. Field for field the same as
// FluxionShadow in Fluxion/Frame.jsl, and packed by hand for the same
// reason the light above is.
typedef struct FluxionRenderShadowGPU
{
    f32 lightRows[4][4]; // the light's view-projection, a row at a time
    f32 atlasRect[4];    // xy corner, zw size, in atlas coordinates
    f32 range[4];        // x which light, y how far it covers, z the handover band, w spare
    f32 bias[4];         // x along the light's axis, y along the normal, zw spare
} FluxionRenderShadowGPU;

#define FLUXION_RENDER_VIEW_SHADOW_BYTES ((usize)FLUXION_RENDER_VIEW_MAX_SHADOWS * sizeof(FluxionRenderShadowGPU))

// How many lights a view starts with room for. Not a limit -- the storage
// grows -- only the point at which growing starts.
// Nine coefficients, four floats each. The fourth is unused and written
// all the same: a three-float element in a storage buffer is padded to
// four by every backend's rules anyway, so writing the padding down is
// what stops each of them from deciding where the next element starts.
#define FLUXION_RENDER_VIEW_IRRADIANCE_COEFFICIENTS 9
#define FLUXION_RENDER_VIEW_IRRADIANCE_BYTES ((usize)FLUXION_RENDER_VIEW_IRRADIANCE_COEFFICIENTS * sizeof(FluxionVec4))

#define FLUXION_RENDER_VIEW_INITIAL_LIGHTS 8

// WHERE MIDDLE GREY LANDS. Eighteen percent of the way to white is what
// every hand-held light meter is calibrated to and what a photographer
// means by a correct exposure -- so a caller who says nothing gets the
// answer a camera would have given.
#define FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_KEY 0.18f

// HOW FAR THE OCCLUSION SEARCH REACHES, in the units the world is in.
// Half a metre is about the size of the creases a room has in it -- where
// a wall meets a floor, where a table meets its leg -- and everything it
// reaches has to be on the screen, which a larger radius often is not.
#define FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_RADIUS 0.5f

// Two directions and four steps along each side of each: sixteen samples
// a pixel. More directions beats more steps at the same total -- the
// noise too few directions leaves is structured and the eye finds it,
// where the noise too few steps leaves is fine and the blur after the
// search takes it out.
#define FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_SLICES 2u
#define FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_STEPS 4u

// How much of the remaining distance the camera covers in a second. Most
// of it, but not all: an eye takes a moment to adjust to a bright room,
// and a camera that arrived instantly would read as the picture flashing
// rather than as the light changing.
#define FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_SPEED 0.9f

// The range the measurement may ask for, in the same multiplier the
// exposure itself is in. BOTH ENDS MATTER: a frame of pure black would
// otherwise ask for an infinite exposure and get it, and the frame after
// it -- with anything at all in it -- would be pure white.
#define FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_LOWEST 0.01f
#define FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_HIGHEST 100.0f

static FluxionRenderViewRecord s_renderViews[FLUXION_RENDERER_MAX_RENDER_VIEWS];

// Both buffers and the bind group together: the group names the buffer,
// so a buffer replaced without the group is a group pointing at something
// that no longer exists.
static bool Fluxion_RenderViewInternal_MakeLightStorage(FluxionRHIDeviceHandle device, u32 capacity,
                                                        FluxionRHIBufferHandle* outStaging, FluxionRHIBufferHandle* outStorage)
{
    const usize bytes = (usize)capacity * sizeof(FluxionRenderLightGPU);

    FluxionRHIBufferDesc stagingDesc;
    stagingDesc.size = bytes;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.RenderView.LightStaging";
    *outStaging = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outStaging)) return false;

    // GPU-only, and that is forced rather than chosen: a buffer the CPU
    // can write lives in memory that at least one backend refuses to give
    // a structured view of. So the CPU writes one buffer, the GPU reads
    // another, and a recorded copy joins them.
    FluxionRHIBufferDesc storageDesc;
    storageDesc.size = bytes;
    storageDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    storageDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    storageDesc.debugName = "Fluxion.RenderView.Lights";
    *outStorage = Fluxion_RHI_CreateBuffer(device, &storageDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outStorage))
    {
        Fluxion_RHI_DestroyBuffer(*outStaging);
        return false;
    }

    return true;
}

static FluxionRHIBindGroupHandle Fluxion_RenderViewInternal_MakeFrameBindGroup(FluxionRHIDeviceHandle device,
                                                                               FluxionRHIBindGroupLayoutHandle layout,
                                                                               FluxionRHIBufferHandle constants,
                                                                               FluxionRHITextureViewHandle environmentView,
                                                                               FluxionRHISamplerHandle environmentSampler,
                                                                               FluxionRHITextureViewHandle prefilteredView,
                                                                               FluxionRHITextureViewHandle dfgView,
                                                                               FluxionRHISamplerHandle tableSampler,
                                                                               FluxionRHITextureViewHandle shadowAtlasView,
                                                                               FluxionRHISamplerHandle shadowSampler,
                                                                               FluxionRHITextureViewHandle occlusionView,
                                                                               FluxionRHISamplerHandle occlusionSampler,
                                                                               FluxionRHIBufferHandle lights, u32 lightCapacity,
                                                                               FluxionRHIBufferHandle irradiance,
                                                                               FluxionRHIBufferHandle shadows)
{
    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupEntry entries[14];
    memset(entries, 0, sizeof(entries));

    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].buffer = constants;
    entries[0].bufferSize = sizeof(FluxionFrameConstants);
    entries[0].textureView = noView;
    entries[0].sampler = noSampler;

    // The numbers here are the shader compiler's, not this file's: a
    // group's uniform buffer is binding 0, then every texture takes a
    // pair, and storage buffers come last. See MakeFrameLayoutDesc.
    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[1].buffer = noBuffer;
    entries[1].textureView = environmentView;
    entries[1].sampler = noSampler;

    entries[2].binding = 2;
    entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[2].buffer = noBuffer;
    entries[2].textureView = noView;
    entries[2].sampler = environmentSampler;

    entries[3].binding = 3;
    entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[3].buffer = noBuffer;
    entries[3].textureView = prefilteredView;
    entries[3].sampler = noSampler;

    // The environment's own sampler for the chain too: trilinear with
    // clamped edges is exactly what reading between roughness mips wants.
    entries[4].binding = 4;
    entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[4].buffer = noBuffer;
    entries[4].textureView = noView;
    entries[4].sampler = tableSampler;

    entries[5].binding = 5;
    entries[5].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[5].buffer = noBuffer;
    entries[5].textureView = dfgView;
    entries[5].sampler = noSampler;

    entries[6].binding = 6;
    entries[6].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[6].buffer = noBuffer;
    entries[6].textureView = noView;
    entries[6].sampler = tableSampler;

    entries[7].binding = 7;
    entries[7].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[7].buffer = noBuffer;
    entries[7].textureView = shadowAtlasView;
    entries[7].sampler = noSampler;

    // The comparison sampler, which is what makes a read of the atlas an
    // answer rather than a depth.
    entries[8].binding = 8;
    entries[8].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[8].buffer = noBuffer;
    entries[8].textureView = noView;
    entries[8].sampler = shadowSampler;

    // The occlusion, and a plain sampler for it. Bound whether or not
    // anything measured it -- see MakeFrameLayoutDesc and Frame.jsl.
    entries[9].binding = 9;
    entries[9].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[9].buffer = noBuffer;
    entries[9].textureView = occlusionView;
    entries[9].sampler = noSampler;

    entries[10].binding = 10;
    entries[10].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[10].buffer = noBuffer;
    entries[10].textureView = noView;
    entries[10].sampler = occlusionSampler;

    entries[11].binding = 11;
    entries[11].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    entries[11].buffer = lights;
    entries[11].bufferSize = (usize)lightCapacity * sizeof(FluxionRenderLightGPU);
    entries[11].textureView = noView;
    entries[11].sampler = noSampler;

    // How big one light is, said rather than assumed. A backend that
    // describes a buffer by element cannot work it out, and a wrong guess
    // reads the right memory in the wrong pieces.
    entries[11].bufferElementStride = (u32)sizeof(FluxionRenderLightGPU);

    entries[12].binding = 12;
    entries[12].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    entries[12].buffer = irradiance;
    entries[12].bufferSize = FLUXION_RENDER_VIEW_IRRADIANCE_BYTES;
    entries[12].textureView = noView;
    entries[12].sampler = noSampler;
    entries[12].bufferElementStride = (u32)sizeof(FluxionVec4);

    entries[13].binding = 13;
    entries[13].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    entries[13].buffer = shadows;
    entries[13].bufferSize = FLUXION_RENDER_VIEW_SHADOW_BYTES;
    entries[13].textureView = noView;
    entries[13].sampler = noSampler;
    entries[13].bufferElementStride = (u32)sizeof(FluxionRenderShadowGPU);

    FluxionRHIBindGroupDesc desc;
    desc.layout = layout;
    desc.entries = entries;
    desc.entryCount = 14;
    return Fluxion_RHI_CreateBindGroup(device, &desc);
}

static FluxionRenderViewRecord* Fluxion_RenderViewInternal_Resolve(FluxionRenderViewHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_RENDER_VIEWS) return NULL;
    FluxionRenderViewRecord* record = &s_renderViews[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

// EVERYTHING IN A DESCRIPTION THAT IS ONLY A NUMBER -- which is all of it
// except the shadow atlas, the one part the device has to be asked for.
// Written once and used twice: by Create, and by the update that hands a
// living view the next frame's description without building it again.
static void Fluxion_RenderViewInternal_ApplyDescription(FluxionRenderViewRecord* record, const FluxionRenderViewDesc* desc)
{
    record->viewMatrix = desc->viewMatrix;
    record->projectionMatrix = desc->projectionMatrix;
    record->viewport = desc->viewport;
    record->scissor = desc->scissor;
    record->renderTarget = desc->renderTarget;
    record->layerMask = desc->layerMask;
    record->ambientColor = desc->ambientColor;
    record->cullDistance = desc->cullDistance;

    // Taken as one when nobody said. See the field's own comment: an
    // unset multiplier producing a black screen would read as a broken
    // renderer rather than as a description with a hole in it.
    record->exposure = desc->exposure > 0.0f ? desc->exposure : 1.0f;
    record->tonemapWhitePoint = desc->tonemapWhitePoint;

    // A THRESHOLD OF ZERO WOULD MAKE EVERYTHING GLOW, so an unset one is
    // read as "one", which is where a screen runs out of range and the
    // usual place for a glow to begin. The knee is a quarter of it, and
    // the amount added back defaults to none: a caller that has not
    // asked for a glow does not get one by leaving a field at zero.
    record->bloomThreshold = desc->bloomThreshold > 0.0f ? desc->bloomThreshold : 1.0f;
    record->bloomKnee = desc->bloomKnee > 0.0f ? desc->bloomKnee : record->bloomThreshold * 0.25f;
    record->bloomIntensity = desc->bloomIntensity;

    // Copied across as they were given. NOTHING IS CORRECTED HERE, and
    // that is the point of the description holding distances rather than
    // values: zero is already the answer for "leave it alone", so there
    // is no unset case to guess at -- unlike the exposure above, where
    // zero would have meant a black screen.
    // The engine's own where nobody said. Unlike the grading below, these
    // have no neutral at zero: a key of nothing would put middle grey at
    // black, and a speed of nothing would freeze the camera.
    record->occlusionRadius = desc->occlusionRadius > 0.0f ? desc->occlusionRadius : FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_RADIUS;
    record->occlusionStrength = desc->occlusionStrength > 0.0f ? desc->occlusionStrength : 1.0f;
    record->occlusionSliceCount = desc->occlusionSliceCount > 0 ? desc->occlusionSliceCount : FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_SLICES;
    record->occlusionStepCount = desc->occlusionStepCount > 0 ? desc->occlusionStepCount : FLUXION_RENDER_VIEW_DEFAULT_OCCLUSION_STEPS;

    record->autoExposureKey = desc->autoExposureKey > 0.0f ? desc->autoExposureKey : FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_KEY;
    record->autoExposureSpeed = desc->autoExposureSpeed > 0.0f ? desc->autoExposureSpeed : FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_SPEED;
    record->autoExposureLowest = desc->autoExposureLowest > 0.0f ? desc->autoExposureLowest : FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_LOWEST;
    record->autoExposureHighest = desc->autoExposureHighest > 0.0f ? desc->autoExposureHighest : FLUXION_RENDER_VIEW_DEFAULT_EXPOSURE_HIGHEST;

    // NOT CORRECTED, because zero is a real answer here: it says this
    // caller is not running a clock, and the exposure should take what
    // was measured whole rather than easing towards it.
    record->deltaSeconds = desc->deltaSeconds > 0.0f ? desc->deltaSeconds : 0.0f;

    record->gradeTemperature = desc->gradeTemperature;
    record->gradeTint = desc->gradeTint;
    record->gradeContrast = desc->gradeContrast;
    record->gradeSaturation = desc->gradeSaturation;
    record->gradeLift = desc->gradeLift;
    record->gradeGamma = desc->gradeGamma;
    record->gradeGain = desc->gradeGain;

    record->encodeOutputToSRGB = desc->encodeOutputToSRGB;
}

FluxionRenderViewHandle Fluxion_RenderView_Create(FluxionRHIDeviceHandle device, const FluxionRenderViewDesc* desc)
{
    FluxionRenderViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(desc != NULL);
    if (desc == NULL) return invalid;

    u32 index = FLUXION_RENDERER_MAX_RENDER_VIEWS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_RENDER_VIEWS; ++i)
    {
        if (!s_renderViews[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_RENDER_VIEWS) return invalid;

    // Nothing asked for means the engine's own pair; anything else is
    // taken as given and checked, because an atlas that is not a whole
    // number of tiles across has room in it that nothing can be placed
    // in -- and finding that out when the first shadow goes missing is
    // finding it out too late.
    const u32 shadowAtlasSize = desc->shadowAtlasSize != 0 ? desc->shadowAtlasSize : FLUXION_RENDERER_SHADOW_ATLAS_SIZE;
    const u32 shadowTileSize = desc->shadowTileSize != 0 ? desc->shadowTileSize : FLUXION_RENDERER_SHADOW_TILE_SIZE;
    if (shadowTileSize == 0 || shadowAtlasSize < shadowTileSize || (shadowAtlasSize % shadowTileSize) != 0)
    {
        FLUXION_LOG_ERROR("RenderView", "a %u texel shadow atlas cannot be cut into %u texel tiles", shadowAtlasSize, shadowTileSize);
        return invalid;
    }

    FluxionRHIBufferDesc bufferDesc;
    bufferDesc.size = sizeof(FluxionFrameConstants);
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    bufferDesc.debugName = "Fluxion.RenderView.FrameConstantBuffer";
    FluxionRHIBufferHandle frameConstantBuffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);

    FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    FluxionRHIBindGroupLayoutHandle frameBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(device, &layoutDesc);

    // Made now rather than when the first light arrives: the bind group
    // names this buffer, and a group with a hole in it is not a group a
    // backend will accept. A view with no lights binds an empty list,
    // which is a real answer.
    FluxionRHIBufferHandle lightStaging;
    FluxionRHIBufferHandle lightStorage;
    if (!Fluxion_RenderViewInternal_MakeLightStorage(device, FLUXION_RENDER_VIEW_INITIAL_LIGHTS, &lightStaging, &lightStorage))
    {
        Fluxion_RHI_DestroyBindGroupLayout(frameBindGroupLayout);
        Fluxion_RHI_DestroyBuffer(frameConstantBuffer);
        return invalid;
    }

    // The black cube the engine keeps, until somebody says otherwise. A
    // binding with nothing in it is refused as a whole group, so there is
    // no such thing as a view with no environment.
    const FluxionRHITextureViewHandle defaultEnvironment = Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_BLACK_CUBE);

    FluxionRHISamplerDesc environmentSamplerDesc;
    memset(&environmentSamplerDesc, 0, sizeof(environmentSamplerDesc));
    environmentSamplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    environmentSamplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    environmentSamplerDesc.mipFilter = FLUXION_RHI_FILTER_LINEAR;
    environmentSamplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    environmentSamplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    environmentSamplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    environmentSamplerDesc.maxAnisotropy = 1.0f;
    environmentSamplerDesc.debugName = "Fluxion.RenderView.EnvironmentSampler";
    // Made here and destroyed with the view, rather than taken from the
    // shared sampler cache. The cache is a separate facility with a
    // lifetime of its own, and quietly putting an entry in it would mean
    // that making a view obliged the caller to shut something else down
    // -- a dependency nothing about creating a view announces. One
    // sampler per view is a handful of objects.
    const FluxionRHISamplerHandle defaultSampler = Fluxion_RHI_CreateSampler(device, &environmentSamplerDesc);

    // Nine coefficients, device-side only. Nothing writes it from here
    // and nothing reads it back: a compute pass fills it and the surfaces
    // read it, both on the device.
    FluxionRHIBufferDesc irradianceDesc;
    irradianceDesc.size = FLUXION_RENDER_VIEW_IRRADIANCE_BYTES;
    irradianceDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER;
    irradianceDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    irradianceDesc.debugName = "Fluxion.RenderView.Irradiance";
    const FluxionRHIBufferHandle irradianceBuffer = Fluxion_RHI_CreateBuffer(device, &irradianceDesc);

    // The prefiltered chain and the split-sum table, empty until the
    // renderer's passes fill them -- which the UpdateEnvironment contract
    // puts before the first draw that could read them. Uncompressed
    // floats: both are written by copies from a compute pass's buffer,
    // and both together are a couple of megabytes per view.
    FluxionRHITextureDesc prefilteredDesc;
    memset(&prefilteredDesc, 0, sizeof(prefilteredDesc));
    prefilteredDesc.width = FLUXION_RENDERER_PREFILTERED_SIZE;
    prefilteredDesc.height = FLUXION_RENDERER_PREFILTERED_SIZE;
    prefilteredDesc.depth = 1;
    prefilteredDesc.mipLevels = FLUXION_RENDERER_PREFILTERED_MIPS;
    prefilteredDesc.arrayLayers = FLUXION_RHI_CUBE_FACE_COUNT;
    prefilteredDesc.sampleCount = 1;
    prefilteredDesc.format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    prefilteredDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    prefilteredDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    prefilteredDesc.debugName = "Fluxion.RenderView.PrefilteredEnvironment";
    prefilteredDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;
    const FluxionRHITextureHandle prefilteredTexture = Fluxion_RHI_CreateTexture(device, &prefilteredDesc);

    FluxionRHITextureViewDesc prefilteredViewDesc;
    memset(&prefilteredViewDesc, 0, sizeof(prefilteredViewDesc));
    prefilteredViewDesc.texture = prefilteredTexture;
    prefilteredViewDesc.format = prefilteredDesc.format;
    prefilteredViewDesc.mipLevelCount = FLUXION_RENDERER_PREFILTERED_MIPS;
    prefilteredViewDesc.arrayLayerCount = FLUXION_RHI_CUBE_FACE_COUNT;
    prefilteredViewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;
    const FluxionRHITextureViewHandle prefilteredView = Fluxion_RHI_CreateTextureView(device, &prefilteredViewDesc);

    FluxionRHITextureDesc dfgDesc;
    memset(&dfgDesc, 0, sizeof(dfgDesc));
    dfgDesc.width = FLUXION_RENDERER_DFG_SIZE;
    dfgDesc.height = FLUXION_RENDERER_DFG_SIZE;
    dfgDesc.depth = 1;
    dfgDesc.mipLevels = 1;
    dfgDesc.arrayLayers = 1;
    dfgDesc.sampleCount = 1;
    dfgDesc.format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    dfgDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    dfgDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    dfgDesc.debugName = "Fluxion.RenderView.DfgTable";
    const FluxionRHITextureHandle dfgTexture = Fluxion_RHI_CreateTexture(device, &dfgDesc);

    FluxionRHITextureViewDesc dfgViewDesc;
    memset(&dfgViewDesc, 0, sizeof(dfgViewDesc));
    dfgViewDesc.texture = dfgTexture;
    dfgViewDesc.format = dfgDesc.format;
    dfgViewDesc.mipLevelCount = 1;
    dfgViewDesc.arrayLayerCount = 1;
    const FluxionRHITextureViewHandle dfgView = Fluxion_RHI_CreateTextureView(device, &dfgViewDesc);

    // One depth texture for every shadow this view draws. Sampled as
    // well as drawn into -- the combination nothing in this engine asked
    // a backend for before the comparison sampling arrived.
    FluxionRHITextureDesc shadowDesc;
    memset(&shadowDesc, 0, sizeof(shadowDesc));
    shadowDesc.width = shadowAtlasSize;
    shadowDesc.height = shadowAtlasSize;
    shadowDesc.depth = 1;
    shadowDesc.mipLevels = 1;
    shadowDesc.arrayLayers = 1;
    shadowDesc.sampleCount = 1;
    shadowDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    shadowDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    shadowDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    shadowDesc.debugName = "Fluxion.RenderView.ShadowAtlas";
    const FluxionRHITextureHandle shadowAtlasTexture = Fluxion_RHI_CreateTexture(device, &shadowDesc);

    FluxionRHITextureViewDesc shadowViewDesc;
    memset(&shadowViewDesc, 0, sizeof(shadowViewDesc));
    shadowViewDesc.texture = shadowAtlasTexture;
    shadowViewDesc.format = shadowDesc.format;
    shadowViewDesc.mipLevelCount = 1;
    shadowViewDesc.arrayLayerCount = 1;
    const FluxionRHITextureViewHandle shadowAtlasView = Fluxion_RHI_CreateTextureView(device, &shadowViewDesc);

    // Reading the atlas asks a question -- "did the light get this far" --
    // and the hardware answers it as part of the fetch. LESS_OR_EQUAL
    // because depth counts up from the near plane: a surface at or in
    // front of what was recorded is the one the light reached.
    //
    // Linear filtering on a comparison sampler averages the ANSWERS of
    // the four texels round the point rather than their depths, which is
    // the whole reason to ask the hardware instead of reading depths and
    // comparing them here -- an average of depths is a number no surface
    // was ever at.
    FluxionRHISamplerDesc shadowSamplerDesc;
    memset(&shadowSamplerDesc, 0, sizeof(shadowSamplerDesc));
    shadowSamplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    shadowSamplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    shadowSamplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    shadowSamplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSamplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSamplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSamplerDesc.maxAnisotropy = 1.0f;
    shadowSamplerDesc.compareEnable = true;
    shadowSamplerDesc.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    shadowSamplerDesc.debugName = "Fluxion.RenderView.ShadowSampler";
    const FluxionRHISamplerHandle shadowSampler = Fluxion_RHI_CreateSampler(device, &shadowSamplerDesc);

    // Fixed at the most shadows a view may hold, rather than grown like
    // the light list: the atlas caps how many there can be, so the buffer
    // is already as big as it will ever need to be.
    FluxionRHIBufferDesc shadowStagingDesc;
    shadowStagingDesc.size = FLUXION_RENDER_VIEW_SHADOW_BYTES;
    shadowStagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    shadowStagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    shadowStagingDesc.debugName = "Fluxion.RenderView.ShadowStaging";
    const FluxionRHIBufferHandle shadowStaging = Fluxion_RHI_CreateBuffer(device, &shadowStagingDesc);

    FluxionRHIBufferDesc shadowStorageDesc;
    shadowStorageDesc.size = FLUXION_RENDER_VIEW_SHADOW_BYTES;
    shadowStorageDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    shadowStorageDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    shadowStorageDesc.debugName = "Fluxion.RenderView.Shadows";
    const FluxionRHIBufferHandle shadowStorage = Fluxion_RHI_CreateBuffer(device, &shadowStorageDesc);

    // WHITE UNTIL SOMETHING MEASURES IT. White is "all of the sky reaches
    // here", which multiplies nothing away -- so a view nobody hands an
    // occlusion to is lit exactly as it was before any of this existed.
    const FluxionRHITextureViewHandle occlusionView = Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE);

    FluxionRHIBindGroupHandle frameBindGroup = Fluxion_RenderViewInternal_MakeFrameBindGroup(
        device, frameBindGroupLayout, frameConstantBuffer, defaultEnvironment, defaultSampler,
        prefilteredView, dfgView, defaultSampler, shadowAtlasView, shadowSampler,
        occlusionView, defaultSampler,
        lightStorage, FLUXION_RENDER_VIEW_INITIAL_LIGHTS, irradianceBuffer, shadowStorage);

    FluxionRenderViewRecord* record = &s_renderViews[index];
    u32 generation = record->generation;
    memset(record, 0, sizeof(*record));
    record->alive = true;
    record->generation = generation;
    record->device = device;
    Fluxion_RenderViewInternal_ApplyDescription(record, desc);

    record->frameConstantBuffer = frameConstantBuffer;
    record->frameBindGroupLayout = frameBindGroupLayout;
    record->frameBindGroup = frameBindGroup;
    record->environmentView = defaultEnvironment;

    // One until something says otherwise. The default environment is a
    // black cube, so this multiplies nothing -- but a view that was given
    // a real one and no intensity would otherwise start out invisible.
    record->environmentIntensity = 1.0f;
    record->occlusionView = occlusionView;
    record->occlusionMeasured = false;
    record->environmentSampler = defaultSampler;
    record->ownedEnvironmentSampler = defaultSampler;
    record->irradianceBuffer = irradianceBuffer;
    record->prefilteredTexture = prefilteredTexture;
    record->prefilteredView = prefilteredView;
    record->dfgTexture = dfgTexture;
    record->dfgView = dfgView;
    record->dfgWanted = true;
    record->shadowAtlasTexture = shadowAtlasTexture;
    record->shadowAtlasView = shadowAtlasView;
    record->shadowAtlasSize = shadowAtlasSize;
    record->shadowTileSize = shadowTileSize;
    record->shadowSampler = shadowSampler;
    record->shadowStaging = shadowStaging;
    record->shadowStorage = shadowStorage;

    // A fresh view starts with the black cube, whose nine coefficients are
    // nine zeroes -- but the buffer has never been written, so what is in
    // it is whatever the allocator handed over. Projecting once before
    // anything reads it is what makes "no environment" mean black rather
    // than mean nothing in particular.
    record->environmentDirty = true;

    record->lightStaging = lightStaging;
    record->lightStorage = lightStorage;
    record->lightCapacity = FLUXION_RENDER_VIEW_INITIAL_LIGHTS;
    record->lightCount = 0;

    FluxionRenderViewHandle handle = { index, generation };
    return handle;
}

bool Fluxion_RenderView_UpdateDescription(FluxionRenderViewHandle view, const FluxionRenderViewDesc* desc)
{
    FLUXION_ASSERT(desc != NULL);
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || desc == NULL) return false;

    // THE ONE THING THAT CANNOT BE CHANGED IN PLACE. The atlas is a
    // texture, the tiling is how the shadows are laid out in it, and the
    // frame bind group names that texture -- a different size is a
    // different texture, and a caller asking for one is asking for a
    // different view. Zero means "whatever this view already has", so a
    // caller who never filled these fields in is never refused.
    const u32 wantedAtlas = desc->shadowAtlasSize != 0 ? desc->shadowAtlasSize : record->shadowAtlasSize;
    const u32 wantedTile = desc->shadowTileSize != 0 ? desc->shadowTileSize : record->shadowTileSize;
    if (wantedAtlas != record->shadowAtlasSize || wantedTile != record->shadowTileSize) return false;

    // Everything else is numbers the next UpdateFrameConstants uploads.
    // NOT TOUCHED HERE: the environment, the lights, the previous frame's
    // matrix -- none of them are in a description, and all of them are
    // things a view is told separately and should not silently lose by
    // being handed the next frame's camera.
    Fluxion_RenderViewInternal_ApplyDescription(record, desc);
    return true;
}

void Fluxion_RenderView_Destroy(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_RenderView_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    if (FLUXION_HANDLE_IS_VALID(record->ownedEnvironmentSampler)) Fluxion_RHI_DestroySampler(record->ownedEnvironmentSampler);
    if (FLUXION_HANDLE_IS_VALID(record->irradianceBuffer)) Fluxion_RHI_DestroyBuffer(record->irradianceBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->prefilteredView)) Fluxion_RHI_DestroyTextureView(record->prefilteredView);
    if (FLUXION_HANDLE_IS_VALID(record->prefilteredTexture)) Fluxion_RHI_DestroyTexture(record->prefilteredTexture);
    if (FLUXION_HANDLE_IS_VALID(record->dfgView)) Fluxion_RHI_DestroyTextureView(record->dfgView);
    if (FLUXION_HANDLE_IS_VALID(record->dfgTexture)) Fluxion_RHI_DestroyTexture(record->dfgTexture);
    if (FLUXION_HANDLE_IS_VALID(record->shadowAtlasView)) Fluxion_RHI_DestroyTextureView(record->shadowAtlasView);
    if (FLUXION_HANDLE_IS_VALID(record->shadowAtlasTexture)) Fluxion_RHI_DestroyTexture(record->shadowAtlasTexture);
    if (FLUXION_HANDLE_IS_VALID(record->shadowSampler)) Fluxion_RHI_DestroySampler(record->shadowSampler);
    if (FLUXION_HANDLE_IS_VALID(record->shadowStorage)) Fluxion_RHI_DestroyBuffer(record->shadowStorage);
    if (FLUXION_HANDLE_IS_VALID(record->shadowStaging)) Fluxion_RHI_DestroyBuffer(record->shadowStaging);
    if (FLUXION_HANDLE_IS_VALID(record->lightStorage)) Fluxion_RHI_DestroyBuffer(record->lightStorage);
    if (FLUXION_HANDLE_IS_VALID(record->lightStaging)) Fluxion_RHI_DestroyBuffer(record->lightStaging);
    if (FLUXION_HANDLE_IS_VALID(record->frameBindGroup)) Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
    if (FLUXION_HANDLE_IS_VALID(record->frameBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->frameBindGroupLayout);
    if (FLUXION_HANDLE_IS_VALID(record->frameConstantBuffer)) Fluxion_RHI_DestroyBuffer(record->frameConstantBuffer);

    record->alive = false;
    ++record->generation;
}

void Fluxion_RenderView_SetPreviousViewProjection(FluxionRenderViewHandle view, FluxionMat4 previousViewProjection)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    record->previousViewProjection = previousViewProjection;
    record->hasPreviousViewProjection = true;
}

FluxionMat4 Fluxion_RenderView_GetViewProjection(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return Fluxion_Mat4_Identity();

    return Fluxion_Mat4_Multiply(record->projectionMatrix, record->viewMatrix);
}

void Fluxion_RenderView_UpdateFrameConstants(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    FluxionFrameConstants constants;
    memset(&constants, 0, sizeof(constants));

    constants.viewProjection = Fluxion_Mat4_Multiply(record->projectionMatrix, record->viewMatrix);

    // Worked out from the view matrix rather than asked for separately.
    // A camera position given alongside a view matrix is a second place
    // to say where the camera is, and the two disagreeing produces
    // lighting that is wrong in a way nothing reports -- highlights in
    // the wrong place, which reads as an artist's mistake.
    const FluxionMat4 cameraToWorld = Fluxion_Mat4_RigidInverse(record->viewMatrix);
    constants.cameraPosition.x = cameraToWorld.m[0][3];
    constants.cameraPosition.y = cameraToWorld.m[1][3];
    constants.cameraPosition.z = cameraToWorld.m[2][3];
    constants.cameraPosition.w = 1.0f;

    constants.ambientColor.x = record->ambientColor.x;
    constants.ambientColor.y = record->ambientColor.y;
    constants.ambientColor.z = record->ambientColor.z;

    // In the spare component of the ambient rather than in one of its
    // own. The two stand for the same thing at different stages -- a flat
    // amount arriving from everywhere, and the real thing that replaces
    // it -- so keeping them together is what will make the changeover one
    // edit rather than a hunt.
    constants.ambientColor.w = record->environmentIntensity;

    // WHETHER ANYTHING MEASURED THE OCCLUSION, and which way this
    // backend's rows run for a surface reading a texture a fullscreen
    // pass wrote -- see Frame.jsl. Both are the frame's, not a material's.
    constants.screenParams.x = record->occlusionMeasured ? 1.0f : 0.0f;
    // ONE WHERE A SURFACE MUST TURN ITS VERTICAL COORDINATE OVER to read
    // a screen-space texture, and that is the backends whose framebuffer
    // starts at the TOP -- which is the two that are not OpenGL.
    //
    // Measured rather than reasoned: the same triangle, read back from
    // the same texture, sat on row 36.8 on those two and row 26.2 on
    // OpenGL, on a frame sixty-four rows tall. Its clip-space middle is
    // at minus a sixth, which is 26.7 counting down from the bottom.
    constants.screenParams.y =
        Fluxion_RHI_GetDeviceBackendType(record->device) == FLUXION_RHI_BACKEND_OPENGL ? 0.0f : 1.0f;
    constants.screenParams.z = 0.0f;
    constants.screenParams.w = 0.0f;

    // A multiplier of one, no curve, and no transfer function, when the
    // resolve at the end of the frame is going to do all three. What the
    // passes then write is the light itself -- which is the whole point
    // of the extra target: anything that reads the picture as light has
    // something to read.
    constants.toneMapping.x = record->toneMappingDeferred ? 1.0f : record->exposure;
    constants.toneMapping.y = record->toneMappingDeferred ? 0.0f : record->tonemapWhitePoint;
    constants.toneMapping.z = (!record->toneMappingDeferred && record->encodeOutputToSRGB) ? 1.0f : 0.0f;

    // Worked out once a frame here rather than in every pixel that wants
    // it. Turning a point on the screen back into a direction in the
    // world is the whole of drawing a sky.
    constants.inverseViewProjection = Fluxion_Mat4_Inverse(constants.viewProjection);

    // THE TRANSPOSE HAPPENS HERE, once, at the upload boundary. Callers
    // hand over ordinary row-major matrices, the way the arithmetic
    // writes them; both shader languages read uniform matrices
    // column-major. Before this lived here, every caller had to remember
    // it -- and the three callers had settled on three different answers,
    // each one working only on the inputs it happened to be given.
    // The previous frame's, before this one is transposed away -- and
    // this frame's own when nobody said otherwise, which is a frame
    // where nothing has moved.
    constants.previousViewProjection =
        Fluxion_Mat4_Transposed(record->hasPreviousViewProjection ? record->previousViewProjection : constants.viewProjection);

    constants.viewProjection = Fluxion_Mat4_Transposed(constants.viewProjection);
    constants.inverseViewProjection = Fluxion_Mat4_Transposed(constants.inverseViewProjection);

    constants.lightParams.x = (f32)record->lightCount;
    constants.lightParams.y = (f32)record->shadowCount;

    constants.shadowAtlasParams.x = 1.0f / (f32)record->shadowAtlasSize;

    // Every shadow rectangle in this engine counts rows downwards from
    // the top, because that is how the shadow pass's viewport places them
    // -- the RHI has one viewport convention on every backend. One
    // backend then stores its texture rows the other way up, so a read of
    // the atlas has to be turned over on the way in, and this pair is the
    // only place that says so.
    const bool rowsRunDownwards = Fluxion_RHI_GetDeviceBackendType(record->device) != FLUXION_RHI_BACKEND_OPENGL;
    constants.shadowAtlasParams.y = rowsRunDownwards ? 1.0f : -1.0f;
    constants.shadowAtlasParams.z = rowsRunDownwards ? 0.0f : 1.0f;

    void* mapped = Fluxion_RHI_MapBuffer(record->frameConstantBuffer);
    if (mapped != NULL)
    {
        memcpy(mapped, &constants, sizeof(constants));
        Fluxion_RHI_UnmapBuffer(record->frameConstantBuffer);
    }
}

void Fluxion_RenderView_SetLights(FluxionRenderViewHandle view, const FluxionRenderLight* lights, u32 count)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;
    if (count > 0 && lights == NULL) return;

    // Grown to fit, and never shrunk. A scene whose light count wobbles
    // by one from frame to frame would otherwise rebuild two buffers and
    // a bind group twice a second, and the memory saved by shrinking is
    // sixty-four bytes a light.
    if (count > record->lightCapacity)
    {
        u32 capacity = record->lightCapacity;
        while (capacity < count) capacity *= 2;

        FluxionRHIBufferHandle staging;
        FluxionRHIBufferHandle storage;
        if (!Fluxion_RenderViewInternal_MakeLightStorage(record->device, capacity, &staging, &storage)) return;

        FluxionRHIBindGroupHandle group = Fluxion_RenderViewInternal_MakeFrameBindGroup(
            record->device, record->frameBindGroupLayout, record->frameConstantBuffer,
            record->environmentView, record->environmentSampler,
            record->prefilteredView, record->dfgView, record->ownedEnvironmentSampler,
            record->shadowAtlasView, record->shadowSampler,
            record->occlusionView, record->ownedEnvironmentSampler,
            storage, capacity, record->irradianceBuffer, record->shadowStorage);
        if (!FLUXION_HANDLE_IS_VALID(group))
        {
            Fluxion_RHI_DestroyBuffer(storage);
            Fluxion_RHI_DestroyBuffer(staging);
            return;
        }

        // The old ones go through the ordinary deferred destroy: the GPU
        // may still be reading last frame's list out of them.
        Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
        Fluxion_RHI_DestroyBuffer(record->lightStorage);
        Fluxion_RHI_DestroyBuffer(record->lightStaging);

        record->lightStaging = staging;
        record->lightStorage = storage;
        record->frameBindGroup = group;
        record->lightCapacity = capacity;
    }

    record->lightCount = count;
    if (count == 0) return;

    FluxionRenderLightGPU* mapped = (FluxionRenderLightGPU*)Fluxion_RHI_MapBuffer(record->lightStaging);
    if (mapped == NULL) return;

    for (u32 i = 0; i < count; ++i)
    {
        const FluxionRenderLight* light = &lights[i];
        FluxionRenderLightGPU* out = &mapped[i];

        out->positionRange[0] = light->position.x;
        out->positionRange[1] = light->position.y;
        out->positionRange[2] = light->position.z;
        out->positionRange[3] = light->range;

        // Normalized here rather than trusted. A direction that came from
        // a scaled object's transform is not unit length, and a shader
        // that assumed it was would light the surface by however wrong
        // the length happened to be -- which looks like the light being
        // the wrong brightness, not like a scale being wrong.
        FluxionVec3 direction = Fluxion_Vec3_Normalize(light->direction);
        out->direction[0] = direction.x;
        out->direction[1] = direction.y;
        out->direction[2] = direction.z;
        out->direction[3] = (f32)light->type;

        out->color[0] = light->color.x;
        out->color[1] = light->color.y;
        out->color[2] = light->color.z;
        out->color[3] = light->innerConeCos;

        out->cone[0] = light->outerConeCos;
        out->cone[1] = 0.0f;
        out->cone[2] = 0.0f;
        out->cone[3] = 0.0f;
    }

    Fluxion_RHI_UnmapBuffer(record->lightStaging);
}

void Fluxion_RenderView_SetEnvironment(FluxionRenderViewHandle view, FluxionRHITextureViewHandle cubeView,
                                      FluxionRHISamplerHandle sampler, f32 intensity)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    // Stored BEFORE the check below, which returns early when the texture
    // has not changed. The intensity travels in the frame constants and
    // not in the bind group, so it can change while the group stays the
    // same -- and setting it after that return is how a sky that was
    // asked to dim would go on burning.
    //
    // Negative is refused rather than passed on: it would come out as a
    // sky darker than black, which a float will happily hold and no
    // hardware will do anything sensible with.
    record->environmentIntensity = intensity > 0.0f ? intensity : 0.0f;

    // An invalid one puts the black cube back rather than leaving the
    // binding empty. There is no state in which this view has no
    // environment -- see the header.
    FluxionRHITextureViewHandle wanted = cubeView;
    FluxionRHISamplerHandle wantedSampler = sampler;
    if (!FLUXION_HANDLE_IS_VALID(wanted)) wanted = Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_BLACK_CUBE);
    if (!FLUXION_HANDLE_IS_VALID(wantedSampler)) wantedSampler = record->environmentSampler;

    // The group names the view, so changing the view means a new group.
    // Asked first whether anything actually changed: a caller that sets
    // the same environment every frame would otherwise rebuild a bind
    // group every frame, and the old ones would pile up until the next
    // collection.
    if (wanted.index == record->environmentView.index && wanted.generation == record->environmentView.generation &&
        wantedSampler.index == record->environmentSampler.index && wantedSampler.generation == record->environmentSampler.generation)
    {
        return;
    }

    FluxionRHIBindGroupHandle group = Fluxion_RenderViewInternal_MakeFrameBindGroup(
        record->device, record->frameBindGroupLayout, record->frameConstantBuffer,
        wanted, wantedSampler,
        record->prefilteredView, record->dfgView, record->ownedEnvironmentSampler,
        record->shadowAtlasView, record->shadowSampler,
        record->occlusionView, record->ownedEnvironmentSampler,
        record->lightStorage, record->lightCapacity, record->irradianceBuffer, record->shadowStorage);
    if (!FLUXION_HANDLE_IS_VALID(group)) return;

    Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
    record->frameBindGroup = group;
    record->environmentView = wanted;
    record->environmentSampler = wantedSampler;

    // AFTER the early return above, and that placement is the whole
    // point: a caller that sets the same sky every frame would otherwise
    // ask for it to be worked out again every frame -- expensive, and
    // worse than expensive, since the passes that answer overwrite
    // textures the frames still in flight are reading. The intensity
    // above is different: it travels in the frame constants rather than
    // in what those passes produce, so it changes without any of this.
    record->environmentDirty = true;
}

void Fluxion_RenderView_SetAmbientOcclusion(FluxionRenderViewHandle view, FluxionRHITextureViewHandle occlusion, bool measured)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    // Nothing measured it, or what did is gone: back to the white
    // stand-in, which reads as "all of the sky reaches here".
    FluxionRHITextureViewHandle wanted = occlusion;
    if (!FLUXION_HANDLE_IS_VALID(wanted) || !measured)
    {
        wanted = Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE);
        measured = false;
    }

    // Whether it was measured travels in the frame constants rather than
    // in the group, so it is stored before the early return below -- the
    // same trap the environment's intensity is written up in.
    record->occlusionMeasured = measured;

    // THE GROUP NAMES THE TEXTURE, SO CHANGING THE TEXTURE MEANS A NEW
    // GROUP -- and a caller handing over the same one every frame, which
    // is what a frame loop does, would otherwise build one every frame
    // and leave the old ones to pile up.
    if (wanted.index == record->occlusionView.index && wanted.generation == record->occlusionView.generation) return;

    FluxionRHIBindGroupHandle group = Fluxion_RenderViewInternal_MakeFrameBindGroup(
        record->device, record->frameBindGroupLayout, record->frameConstantBuffer,
        record->environmentView, record->environmentSampler,
        record->prefilteredView, record->dfgView, record->ownedEnvironmentSampler,
        record->shadowAtlasView, record->shadowSampler,
        wanted, record->ownedEnvironmentSampler,
        record->lightStorage, record->lightCapacity, record->irradianceBuffer, record->shadowStorage);
    if (!FLUXION_HANDLE_IS_VALID(group)) return;

    Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
    record->frameBindGroup = group;
    record->occlusionView = wanted;
}

FluxionRHIBufferHandle FluxionRendererInternal_RenderView_GetIrradianceBuffer(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHIBufferHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->irradianceBuffer;
}

FluxionRHITextureViewHandle FluxionRendererInternal_RenderView_GetEnvironmentView(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHITextureViewHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->environmentView;
}

FluxionRHISamplerHandle FluxionRendererInternal_RenderView_GetEnvironmentSampler(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHISamplerHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->environmentSampler;
}

// Asking clears it, which is why this takes rather than gets: the one
// caller records a dispatch in answer, and a flag still set afterwards
// would have it recorded again on every frame that followed.
bool FluxionRendererInternal_RenderView_TakeEnvironmentDirty(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    const bool dirty = record->environmentDirty;
    record->environmentDirty = false;
    return dirty;
}

FluxionRHITextureHandle FluxionRendererInternal_RenderView_GetPrefilteredTexture(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHITextureHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->prefilteredTexture;
}

FluxionRHITextureHandle FluxionRendererInternal_RenderView_GetDfgTexture(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHITextureHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->dfgTexture;
}

// Takes, for the same reason as the dirty flag above: the one caller
// records the fill in answer, and the table cannot go stale after.
bool FluxionRendererInternal_RenderView_TakeDfgWanted(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    const bool wanted = record->dfgWanted;
    record->dfgWanted = false;
    return wanted;
}

bool FluxionRendererInternal_RenderView_MarkPrefilteredFilled(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    const bool wasFilled = record->prefilteredFilled;
    record->prefilteredFilled = true;
    return wasFilled;
}

void Fluxion_RenderView_GetShadowAtlasSize(FluxionRenderViewHandle view, u32* outAtlasSize, u32* outTileSize)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    if (outAtlasSize != NULL) *outAtlasSize = record->shadowAtlasSize;
    if (outTileSize != NULL) *outTileSize = record->shadowTileSize;
}

FluxionRHITextureHandle Fluxion_RenderView_GetShadowAtlasTexture(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHITextureHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->shadowAtlasTexture;
}

FluxionRHITextureViewHandle FluxionRendererInternal_RenderView_GetShadowAtlasView(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        const FluxionRHITextureViewHandle none = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return none;
    }
    return record->shadowAtlasView;
}

u32 FluxionRendererInternal_RenderView_GetShadowCount(FluxionRenderViewHandle view)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return 0;
    return record->shadowCount;
}

bool FluxionRendererInternal_RenderView_GetShadow(FluxionRenderViewHandle view, u32 index,
                                                  FluxionMat4* outLightViewProjection, FluxionShadowAtlasTile* outTile)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || index >= record->shadowCount) return false;

    if (outLightViewProjection != NULL) *outLightViewProjection = record->shadowMatrices[index];
    if (outTile != NULL) *outTile = record->shadowTiles[index];
    return true;
}

// One shadow, in the shape a shader reads it.
static void Fluxion_RenderViewInternal_WriteShadow(FluxionRenderShadowGPU* out, const FluxionRenderViewShadow* shadow,
                                                   const FluxionShadowAtlasDesc* atlas, FluxionShadowAtlasTile tile)
{
    // A row at a time, and no transpose: what the shader does with these
    // is four dot products against a position, which is the matrix read
    // exactly as it is written here.
    for (u32 row = 0; row < 4; ++row)
    {
        for (u32 column = 0; column < 4; ++column)
        {
            out->lightRows[row][column] = shadow->lightViewProjection.m[row][column];
        }
    }

    const FluxionVec4 rect = Fluxion_ShadowAtlas_TileRect(atlas, tile);
    out->atlasRect[0] = rect.x;
    out->atlasRect[1] = rect.y;
    out->atlasRect[2] = rect.z;
    out->atlasRect[3] = rect.w;

    out->range[0] = (f32)shadow->lightIndex;
    out->range[1] = shadow->coverTo;
    out->range[2] = shadow->blendBand;

    // How the shader picks among this light's shadows: where the surface
    // is, or how far the eye is.
    out->range[3] = shadow->cubeFaces ? 1.0f : 0.0f;

    out->bias[0] = shadow->depthBias;
    out->bias[1] = shadow->normalBias;
    out->bias[2] = 0.0f;
    out->bias[3] = 0.0f;
}

u32 Fluxion_RenderView_SetShadows(FluxionRenderViewHandle view, const FluxionRenderViewShadow* shadows, u32 count)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return 0;
    if (count > 0 && shadows == NULL) return 0;

    record->shadowCount = 0;
    if (count == 0) return 0;

    FluxionShadowAtlasDesc atlas;
    atlas.atlasSize = record->shadowAtlasSize;
    atlas.tileSize = record->shadowTileSize;

    const u32 offered = count < FLUXION_RENDER_VIEW_MAX_SHADOWS ? count : FLUXION_RENDER_VIEW_MAX_SHADOWS;

    // Grouped by light, because a light is what fits or does not. Runs of
    // one light are already next to each other -- that is the contract --
    // so a group ends where the light index changes.
    FluxionShadowAtlasRequest requests[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    u32 groupFirst[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    u32 groupCount = 0;

    for (u32 i = 0; i < offered; )
    {
        u32 runLength = 1;
        while (i + runLength < offered &&
               shadows[i + runLength].lightIndex == shadows[i].lightIndex &&
               runLength < FLUXION_SHADOW_ATLAS_MAX_TILES_PER_REQUEST)
        {
            ++runLength;
        }

        requests[groupCount].tileCount = runLength;
        requests[groupCount].tag = groupCount;
        groupFirst[groupCount] = i;
        ++groupCount;

        i += runLength;
    }

    FluxionShadowAtlasResult results[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    Fluxion_ShadowAtlas_Allocate(&atlas, requests, groupCount, results);

    FluxionRenderShadowGPU* mapped = (FluxionRenderShadowGPU*)Fluxion_RHI_MapBuffer(record->shadowStaging);
    if (mapped == NULL) return 0;

    u32 written = 0;
    for (u32 group = 0; group < groupCount; ++group)
    {
        if (!results[group].fitted) continue;

        for (u32 slot = 0; slot < results[group].tileCount; ++slot)
        {
            const FluxionRenderViewShadow* shadow = &shadows[groupFirst[group] + slot];
            const FluxionShadowAtlasTile tile = results[group].tiles[slot];

            record->shadowMatrices[written] = shadow->lightViewProjection;
            record->shadowTiles[written] = tile;

            Fluxion_RenderViewInternal_WriteShadow(&mapped[written], shadow, &atlas, tile);
            ++written;
        }
    }

    Fluxion_RHI_UnmapBuffer(record->shadowStaging);
    record->shadowCount = written;
    return written;
}

void Fluxion_RenderView_UploadLighting(FluxionRenderViewHandle view, FluxionRHICommandListHandle commandList)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    // Cleared to "nothing in front of anything", once, before any draw
    // can read it. Everything is lit until a shadow pass says otherwise,
    // which is the right answer for a view that has no shadows at all --
    // and the only one that does not depend on what the memory held.
    if (!record->shadowAtlasPrepared)
    {
        const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBarrier toTarget = { record->shadowAtlasTexture, noBuffer,
                                       FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);

        FluxionRHIRenderingAttachment depthAttachment;
        memset(&depthAttachment, 0, sizeof(depthAttachment));
        depthAttachment.view = record->shadowAtlasView;
        depthAttachment.clear = true;
        depthAttachment.clearColor[0] = 1.0f;

        FluxionRHIRenderingDesc renderingDesc;
        memset(&renderingDesc, 0, sizeof(renderingDesc));
        renderingDesc.depthAttachment = &depthAttachment;
        renderingDesc.width = record->shadowAtlasSize;
        renderingDesc.height = record->shadowAtlasSize;

        Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
        Fluxion_RHI_CommandList_EndRendering(commandList);

        FluxionRHIBarrier toRead = { record->shadowAtlasTexture, noBuffer,
                                     FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

        record->shadowAtlasPrepared = true;
    }

    // Nothing to move, and nothing that needs moving: the counts in the
    // frame constants are zero, so no shader will read a byte of either.
    if (record->lightCount > 0)
    {
        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->lightStaging, 0, record->lightStorage, 0,
                                           (usize)record->lightCount * sizeof(FluxionRenderLightGPU));
    }

    if (record->shadowCount > 0)
    {
        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->shadowStaging, 0, record->shadowStorage, 0,
                                           (usize)record->shadowCount * sizeof(FluxionRenderShadowGPU));
    }
}

bool FluxionRendererInternal_RenderView_GetCamera(FluxionRenderViewHandle view, FluxionMat4* outViewProjection,
                                                  FluxionVec3* outCameraPosition, f32* outCullDistance)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    // Worked out here rather than read from the frame constants: those
    // are transposed for the shading languages, and what a frustum is
    // derived from has to be the matrix as this side writes it.
    if (outViewProjection != NULL) *outViewProjection = Fluxion_Mat4_Multiply(record->projectionMatrix, record->viewMatrix);

    if (outCameraPosition != NULL)
    {
        const FluxionMat4 cameraToWorld = Fluxion_Mat4_RigidInverse(record->viewMatrix);
        outCameraPosition->x = cameraToWorld.m[0][3];
        outCameraPosition->y = cameraToWorld.m[1][3];
        outCameraPosition->z = cameraToWorld.m[2][3];
    }

    if (outCullDistance != NULL) *outCullDistance = record->cullDistance;
    return true;
}

bool FluxionRendererInternal_RenderView_Get(FluxionRenderViewHandle view, FluxionRenderTargetHandle* outRenderTarget, u32* outLayerMask, FluxionRHIBindGroupHandle* outFrameBindGroup)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    if (outRenderTarget != NULL) *outRenderTarget = record->renderTarget;
    if (outLayerMask != NULL) *outLayerMask = record->layerMask;
    if (outFrameBindGroup != NULL) *outFrameBindGroup = record->frameBindGroup;
    return true;
}

void FluxionRendererInternal_RenderView_SetToneMappingDeferred(FluxionRenderViewHandle view, bool deferred)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;
    record->toneMappingDeferred = deferred;
}

bool FluxionRendererInternal_RenderView_GetBloom(FluxionRenderViewHandle view, FluxionVec4* outBloom)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || outBloom == NULL) return false;

    outBloom->x = record->bloomThreshold;
    outBloom->y = record->bloomKnee;
    outBloom->z = record->bloomIntensity;
    outBloom->w = 0.0f;
    return true;
}

bool FluxionRendererInternal_RenderView_GetToneMapping(FluxionRenderViewHandle view, FluxionVec4* outToneMapping)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || outToneMapping == NULL) return false;

    outToneMapping->x = record->exposure;
    outToneMapping->y = record->tonemapWhitePoint;
    outToneMapping->z = record->encodeOutputToSRGB ? 1.0f : 0.0f;
    outToneMapping->w = 0.0f;
    return true;
}

bool FluxionRendererInternal_RenderView_GetOcclusion(FluxionRenderViewHandle view, FluxionVec4* outSettings)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || outSettings == NULL) return false;

    outSettings->x = record->occlusionRadius;
    outSettings->y = record->occlusionStrength;
    outSettings->z = (f32)record->occlusionSliceCount;
    outSettings->w = (f32)record->occlusionStepCount;
    return true;
}

bool FluxionRendererInternal_RenderView_GetMatrices(FluxionRenderViewHandle view, FluxionMat4* outView, FluxionMat4* outProjection)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    if (outView != NULL) *outView = record->viewMatrix;
    if (outProjection != NULL) *outProjection = record->projectionMatrix;
    return true;
}

bool FluxionRendererInternal_RenderView_GetAutoExposure(FluxionRenderViewHandle view, FluxionVec4* outParams,
                                                       f32* outDeltaSeconds)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || outParams == NULL || outDeltaSeconds == NULL) return false;

    outParams->x = record->autoExposureKey;
    outParams->y = record->autoExposureSpeed;
    outParams->z = record->autoExposureLowest;
    outParams->w = record->autoExposureHighest;
    *outDeltaSeconds = record->deltaSeconds;
    return true;
}

bool FluxionRendererInternal_RenderView_GetGrading(FluxionRenderViewHandle view, FluxionVec4* outBalance,
                                                  FluxionVec4* outLift, FluxionVec4* outGamma, FluxionVec4* outGain)
{
    const FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL || outBalance == NULL || outLift == NULL || outGamma == NULL || outGain == NULL) return false;

    outBalance->x = record->gradeTemperature;
    outBalance->y = record->gradeTint;

    // TURNED INTO WHAT THE SHADER MULTIPLIES BY, here rather than there.
    // The description holds distances from neutral so that an unfilled
    // field means "as it was"; a shader wants the number itself, and the
    // conversion belongs on the side that knows why the distances exist.
    outBalance->z = 1.0f + record->gradeContrast;
    outBalance->w = 1.0f + record->gradeSaturation;

    outLift->x = record->gradeLift.x;
    outLift->y = record->gradeLift.y;
    outLift->z = record->gradeLift.z;
    outLift->w = 0.0f;

    outGamma->x = 1.0f + record->gradeGamma.x;
    outGamma->y = 1.0f + record->gradeGamma.y;
    outGamma->z = 1.0f + record->gradeGamma.z;
    outGamma->w = 0.0f;

    outGain->x = 1.0f + record->gradeGain.x;
    outGain->y = 1.0f + record->gradeGain.y;
    outGain->z = 1.0f + record->gradeGain.z;
    outGain->w = 0.0f;
    return true;
}

bool FluxionRendererInternal_RenderView_GetViewport(FluxionRenderViewHandle view, FluxionViewport* outViewport)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    if (outViewport != NULL) *outViewport = record->viewport;
    return true;
}
