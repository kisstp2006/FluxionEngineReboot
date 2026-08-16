#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>

#ifdef __cplusplus
extern "C" {
#endif

// RHI.h has no viewport/scissor-rect type of its own to reuse -- there is
// no Fluxion_RHI_CommandList_SetViewport call anywhere in that contract
// yet, so these stay minimal, Renderer-owned types until the RHI grows a
// real one.
typedef struct FluxionViewport
{
    f32 x, y, width, height;
    f32 minDepth, maxDepth;
} FluxionViewport;

typedef struct FluxionScissorRect
{
    i32 x, y;
    u32 width, height;
} FluxionScissorRect;

FLUXION_DEFINE_HANDLE(FluxionRenderViewHandle);

// --- Lights ---------------------------------------------------------------
//
// What a renderer needs to know about one light, flattened into world
// space. Where it came from -- a component on an object, a script, a file
// -- is not this module's business; Fluxion_Scene_GatherLights produces
// these from a scene, and RenderCore never learns what a scene is.

typedef enum FluxionRenderLightType
{
    // Arrives from one direction, everywhere, with no falloff. `position`
    // and `range` mean nothing to it.
    FLUXION_RENDER_LIGHT_DIRECTIONAL = 0,

    FLUXION_RENDER_LIGHT_POINT,
    FLUXION_RENDER_LIGHT_SPOT,
} FluxionRenderLightType;

typedef struct FluxionRenderLight
{
    FluxionVec3 position; // world space

    // World space, unit length, and THE WAY THE LIGHT TRAVELS -- not the
    // way to the light. One convention, chosen because it is the one a
    // spot light's cone is measured around; the shader turns it round
    // where it needs the other.
    FluxionVec3 direction;

    // Colour and intensity together. There is no separate brightness
    // here, exactly as there is none anywhere else in this engine.
    FluxionVec3 color;

    // Where the contribution reaches zero. Ignored by a directional
    // light, which has no distance to fall off over.
    f32 range;

    // The cosines, not the angles. Worked out once on this side rather
    // than per pixel per light on the other: a cosine is not free, and
    // the angle is never wanted for anything else.
    f32 innerConeCos;
    f32 outerConeCos;

    u32 type; // FluxionRenderLightType
} FluxionRenderLight;

// What is constant for one frame of one view, exactly as the shaders
// below see it.
//
// The layout is public because it is a contract: Fluxion/Frame.jsl
// declares the same fields in the same order, and the two have to agree
// byte for byte. A field is added at the END and never in the middle --
// a shader that declares only the first of them still reads the right
// bytes, which is what lets an older material go on working.
//
// Every entry is four floats wide even where three would do. Uniform
// blocks round a three-component value up to four anyway, and writing the
// padding down is better than leaving each backend's packing rules to
// decide where the next field starts.
typedef struct FluxionFrameConstants
{
    FluxionMat4 viewProjection;

    // Where the eye is, in world space. w is unused.
    FluxionVec4 cameraPosition;

    // The inverse of viewProjection above, worked out on this side once a
    // frame rather than in every pixel that needs it.
    FluxionMat4 inverseViewProjection;

    // A flat amount of light arriving from everywhere, standing in for
    // the sky until there is one. rgb; w is unused.
    //
    // The lights themselves are NOT here any more. One light on the frame
    // was enough while there was one and nobody moved it; they are
    // components on objects now, and they arrive through
    // Fluxion_RenderView_SetLights.
    FluxionVec4 ambientColor;

    // x: how many lights the storage buffer holds. yzw unused.
    //
    // A count rather than a terminator: a shader cannot ask a buffer how
    // long it is on every backend, and a loop that ran until it found a
    // zeroed light would stop at the first switched-off one.
    FluxionVec4 lightParams;

    // x: the exposure multiplier. y: the tone mapping white point, with
    // zero or less meaning no tone mapping. z: one if the pass applies
    // the display's transfer function. w unused.
    FluxionVec4 toneMapping;
} FluxionFrameConstants;

// THE ORDER OF THE FIELDS ABOVE IS THE LAYOUT. It has to be the order
// Fluxion/Frame.jsl declares them in, because that is where the offsets
// come from -- and two orders that disagree do not fail, they read each
// other's numbers.

// `renderPipeline` from the original sketch is deliberately not here -- a
// view doesn't own one; pipeline selection happens per-material/per-pass
// (see FluxionDrawPacket), so an unused field would just sit here idle.
typedef struct FluxionRenderViewDesc
{
    FluxionMat4 viewMatrix;
    FluxionMat4 projectionMatrix;
    FluxionViewport viewport;
    FluxionScissorRect scissor;
    FluxionRenderTargetHandle renderTarget;
    u32 layerMask;

    // One directional light and one flat ambient, which is what a frame
    // can be lit by until there is a light system to hold more.
    //
    // Here rather than somewhere of their own because they are frame
    // frequency: they are the same for every object drawn through this
    // view, which is the definition of what belongs in this buffer. When
    // a real light system arrives it will change how these values GET
    // here and not what a shader reads, because a shader reads
    // Fluxion/Frame.jsl either way.
    // A flat amount of light arriving from every direction, standing in
    // for a sky until there is one. The lights themselves are not here:
    // they are components on objects, gathered with
    // Fluxion_Scene_GatherLights and handed over with
    // Fluxion_RenderView_SetLights.
    FluxionVec3 ambientColor;

    // How much light makes a middle grey. One multiplication, applied
    // after the lighting and before the tone mapping.
    //
    // Zero or less is taken as one rather than as darkness. A description
    // that nobody filled in should give a picture, not a black screen --
    // and a black screen is what an unset multiplier would produce, which
    // reads as a broken renderer rather than as a missing setting.
    // Fluxion_Exposure_FromCamera works one of these out from what a
    // photographer would set instead.
    f32 exposure;

    // The value that tone mapping brings out as exactly one. Everything
    // brighter is clipped rather than compressed, which is what makes a
    // highlight read as a bright thing.
    //
    // Zero or less means DO NOT TONE MAP. That is not a degenerate case:
    // it is how a caller says it will do this itself later, and a curve
    // applied twice is far worse than one applied not at all.
    f32 tonemapWhitePoint;

    // Whether the pass encodes its result for the display before writing
    // it.
    //
    // Set this when the colour target is an ordinary eight-bit format,
    // which stores whatever bits it is given. Leave it off when the
    // target is a floating-point one -- which holds light and wants none
    // of this -- or when the target's own format carries the sRGB curve,
    // because then the hardware does it and doing it here as well encodes
    // twice and washes the picture out.
    //
    // A flag rather than a question asked of the target, and that is a
    // compromise worth naming: the format IS the answer, and the RHI has
    // no way today to ask a texture view what its format is. Until it
    // does, the caller that chose the format says what it chose.
    bool encodeOutputToSRGB;
} FluxionRenderViewDesc;

FluxionRenderViewHandle Fluxion_RenderView_Create(FluxionRHIDeviceHandle device, const FluxionRenderViewDesc* desc);
void Fluxion_RenderView_Destroy(FluxionRenderViewHandle view);

// Recomputes viewProjection = projectionMatrix * viewMatrix and uploads
// it to this view's FRAME-frequency uniform buffer. Create does not call
// this itself (a caller may want to set up several views before touching
// the RHI for any one of them) -- call at least once before a draw using
// this view, and again whenever viewMatrix/projectionMatrix change.
void Fluxion_RenderView_UpdateFrameConstants(FluxionRenderViewHandle view);

// The lights this view is lit by, replacing whatever it had.
//
// Copied, so the caller's array need not outlive the call. The storage
// grows to fit and never shrinks -- a scene whose light count wobbles by
// one from frame to frame would otherwise rebuild its buffer and its
// bind group twice a second for no gain.
//
// Passing zero lights is not an error and not a special case: a scene
// with no lights is a scene lit by nothing but its ambient, which is a
// picture rather than a fault.
void Fluxion_RenderView_SetLights(FluxionRenderViewHandle view, const FluxionRenderLight* lights, u32 count);

// What the world looks like in every direction: a cube map, and the
// sampler to read it with.
//
// An invalid view puts back the small black cube the engine keeps, which
// is what a view starts with. That is not a way of saying "no
// environment" politely -- it is the only way a shader can be written at
// all, since a shader cannot ask whether a texture is bound and a backend
// handed an empty slot refuses the whole group rather than the one
// binding.
//
// `intensity` multiplies what the environment contributes -- both what is
// seen of it directly and, once there is any, the light it casts. One
// number for both, because they are the same thing seen two ways: a sky
// shown brighter than it lights is a sky that does not match its own
// reflections, and nothing in a picture says which of the two is wrong.
void Fluxion_RenderView_SetEnvironment(FluxionRenderViewHandle view, FluxionRHITextureViewHandle cubeView,
                                       FluxionRHISamplerHandle sampler, f32 intensity);

// Records the copy that puts those lights where the GPU can read them.
//
// Separate from SetLights, and not hidden inside it, because it needs a
// command list and SetLights does not. The buffer a shader reads lives in
// memory only the GPU can see -- a buffer the CPU can write cannot also
// be one this backend's structured views are allowed to describe -- so
// there is a copy, and a copy is a recorded command.
//
// Call it inside a recording, before anything that draws with this view.
void Fluxion_RenderView_UploadLights(FluxionRenderViewHandle view, FluxionRHICommandListHandle commandList);

#ifdef __cplusplus
}
#endif
