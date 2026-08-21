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

    // x: how wide one shadow atlas texel is, in atlas coordinates. yz:
    // that atlas's second coordinate, as a scale and an offset -- the one
    // place the engine writes down that one backend stores texture rows
    // the other way up. w unused.
    FluxionVec4 shadowAtlasParams;

    // WHERE THE CAMERA WAS LAST FRAME, as one matrix.
    //
    // Everything temporal starts here: a point of this frame's geometry
    // put through this lands where that point was on the screen a frame
    // ago, and the difference between the two is a motion vector. The
    // camera's own movement is in it, which is why one matrix is enough
    // for a still object seen from a moving camera.
    //
    // A view that was never told about a previous frame carries this
    // frame's matrix here, and then nothing has moved -- which is the
    // right answer for a first frame and for a test that draws one.
    FluxionMat4 previousViewProjection;

    // See Fluxion/Frame.jsl: whether the occlusion was measured, and
    // which way this backend's rows run for a surface reading a texture a
    // fullscreen pass wrote.
    FluxionVec4 screenParams;
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

    // --- what glows -------------------------------------------------------
    //
    // Only meaningful with a pipeline whose "bloom" is on; without the
    // pass these are numbers nothing reads.
    //
    // The threshold is in the same units as everything else the frame
    // holds: an amount of light, before the camera's exposure. Zero is
    // read as one -- a threshold of nothing would make every surface
    // glow. The knee is how far below the threshold the glow fades in
    // rather than switching on; zero is read as a quarter of the
    // threshold, because a hard edge shows up as a rim that crawls along
    // a surface as the camera moves.
    f32 bloomThreshold;
    f32 bloomKnee;

    // How much of the glow is added back on top of the picture. Zero --
    // the value of a field nobody set -- adds none of it.
    f32 bloomIntensity;

    // --- what the frame's own brightness should do to the camera ----------
    //
    // Only meaningful with a pipeline whose auto exposure is on. Without
    // the passes these are numbers nothing reads, and the exposure above
    // is the whole answer.
    //
    // WHAT THIS MULTIPLIES IS THE EXPOSURE ABOVE, rather than replacing
    // it. The camera settings a caller worked out stay the camera
    // settings; what the measurement adds is the part a photographer
    // would do by opening up in a dark room, and a caller who wants only
    // one of the two sets the other to its neutral.

    // What fraction of the way to white a middle grey should land at.
    // Zero is read as the engine's own, which is the eighteen percent
    // every light meter is built around.
    f32 autoExposureKey;

    // How fast the camera catches up, as the fraction of the remaining
    // distance it covers in a second. Zero is read as the engine's own.
    //
    // A SPEED RATHER THAN A NUMBER OF FRAMES, and the distinction is the
    // reason it is written this way: a fraction per frame would adapt at
    // one rate on a fast machine and another on a slow one, which is a
    // look that changes with the hardware.
    f32 autoExposureSpeed;

    // The range the measurement is allowed to ask for. Zero on either
    // means the engine's own bound rather than none: a scene that is
    // entirely black would otherwise ask for an exposure of infinity, and
    // get it.
    f32 autoExposureLowest;
    f32 autoExposureHighest;

    // How long the frame before this one lasted. Zero means DO NOT EASE:
    // the exposure takes the measured answer whole, which is what a
    // caller who is not running a clock should get rather than a camera
    // that never moves.
    f32 deltaSeconds;

    // --- how much of the sky reaches each pixel ---------------------------
    //
    // Only meaningful with a pipeline whose occlusion is on. Without the
    // passes these are numbers nothing reads.

    // How far the search reaches, in the same units the world is in. Zero
    // is read as the engine's own -- half a metre, which is about the
    // size of the creases a room has in it.
    //
    // BIGGER IS NOT BETTER. A radius covers fewer pixels the further away
    // a surface is, so a large one is read from coarse levels where it
    // says little; and everything it reaches has to be on the screen,
    // which a metre away often is not.
    f32 occlusionRadius;

    // How much of the answer to believe. Zero is read as one, which is
    // the amount the arithmetic says; less is a hand on the dial.
    f32 occlusionStrength;

    // How many directions through the hemisphere, and how many steps
    // along each side of each. Zero is read as the engine's own.
    //
    // MORE SLICES BEATS MORE STEPS at the same total: the noise a shortage
    // of directions leaves is structured and the eye finds it, where the
    // noise a shortage of steps leaves is fine and the blur after this
    // removes it.
    u32 occlusionSliceCount;
    u32 occlusionStepCount;

    // --- colour grading ---------------------------------------------------
    //
    // EVERY FIELD HERE IS A DISTANCE FROM LEAVING THE PICTURE ALONE, and
    // that is deliberate. A description is filled in by hand, field by
    // field, and the ones nobody reached must mean "as it was" -- so
    // neutral is zero everywhere, rather than one in some places and zero
    // in others. It also makes the honest values reachable: a saturation
    // whose neutral was one could never be asked to go to grey without
    // colliding with "unset".
    //
    // WHERE EACH HALF IS APPLIED is not a detail. The white balance is a
    // property of the light, so it happens to the light -- before the
    // exposure and the curve. Everything below it is a property of the
    // picture, so it happens to the picture, after the curve and before
    // the display's own transfer function.

    // Which way the light leans. Negative is cooler, positive warmer;
    // tint runs from green to magenta, the axis a white balance cannot
    // fix with temperature alone.
    //
    // A BALANCE BETWEEN THE CHANNELS, NOT A BLACK BODY. There is no
    // colour temperature in kelvin here and no chromatic adaptation
    // model: this leans the primaries against each other, which is what
    // a grading control does and is not what a physically-based white
    // point would do. Named for what it is so nobody reaches for it
    // expecting the other thing.
    f32 gradeTemperature;
    f32 gradeTint;

    // Added to one. Contrast pivots about the middle of the display
    // range, so the mid grey stays where it is and the two ends move
    // apart; saturation at minus one is grey, and there is nothing below
    // that worth having.
    f32 gradeContrast;
    f32 gradeSaturation;

    // The three-way control every grading tool has, under the names it
    // has them under. Gain scales, lift adds, gamma bends what is left:
    //
    //     out = (color * (1 + gain) + lift) ^ (1 / (1 + gamma))
    //
    // which is to say gain moves the highlights, lift moves the shadows,
    // and gamma moves the middle without moving either end.
    FluxionVec3 gradeLift;
    FluxionVec3 gradeGamma;
    FluxionVec3 gradeGain;

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

    // How far this view sees. Beyond it an object is not drawn at all.
    //
    // ZERO MEANS NO LIMIT, which is what a description that never
    // mentioned distance gets -- a view that saw nothing because nobody
    // filled this in would read as a broken renderer rather than as a
    // setting with a hole in it. The far plane still applies either way;
    // this is the cheaper test, made before anything is submitted.
    f32 cullDistance;

    // How big this view's shadow atlas is, and how big one tile of it
    // is, both in texels and both a side of a square.
    //
    // Zero for either means the engine's own default pair, so a caller
    // that has no opinion about shadows does not have to have one. When
    // they are given, the atlas must be a whole number of tiles across
    // -- a remainder would be atlas nobody can address -- and a view
    // asked for an impossible pair is not created.
    //
    // Here rather than settable later because the atlas is a texture,
    // and a texture is made once: changing these means a new view.
    // Fluxion_RenderPipelineAsset_ApplyToViewDesc fills them in from a
    // pipeline's shadow quality.
    u32 shadowAtlasSize;
    u32 shadowTileSize;
} FluxionRenderViewDesc;

FluxionRenderViewHandle Fluxion_RenderView_Create(FluxionRHIDeviceHandle device, const FluxionRenderViewDesc* desc);

// Where the camera was when the frame before this one was drawn.
//
// NOT IN THE DESCRIPTION, because a view is made fresh every frame and a
// description that carried it would make every caller keep the answer.
// The renderer keeps it instead and sets it here -- see
// Fluxion_Renderer_BeginFrame -- so the two can never disagree about
// which frame "previous" means.
//
// Before Fluxion_RenderView_UpdateFrameConstants, which is what puts it
// where a shader can read it.
void Fluxion_RenderView_SetPreviousViewProjection(FluxionRenderViewHandle view, FluxionMat4 previousViewProjection);

// This frame's, worked out the same way the frame constants do it -- so
// that whoever keeps it for next frame keeps the same matrix the shaders
// were given, rather than one built from the same parts a second time.
FluxionMat4 Fluxion_RenderView_GetViewProjection(FluxionRenderViewHandle view);
// THE NEXT FRAME'S DESCRIPTION, WITHOUT BUILDING THE VIEW AGAIN.
//
// A view owns real memory on the device -- a shadow atlas, a prefiltered
// environment chain, a lookup table, bind groups naming all of it -- and
// none of that depends on where the camera is standing. Making a view per
// frame therefore allocates and frees megabytes to move a matrix, which
// in this engine's own sample cost more than every pass in the frame put
// together.
//
// Returns false, and changes nothing, when the description asks for a
// shadow atlas of a different size or tiling: that is the one part of a
// view that is a texture rather than a number, and a caller who wants a
// different one wants a different view. Everything else is taken.
//
// What a view was told SEPARATELY it keeps: its environment, its lights,
// the matrix it was drawn with last frame. A description does not carry
// those, and this does not clear them.
bool Fluxion_RenderView_UpdateDescription(FluxionRenderViewHandle view, const FluxionRenderViewDesc* desc);

void Fluxion_RenderView_Destroy(FluxionRenderViewHandle view);

// Recomputes viewProjection = projectionMatrix * viewMatrix and uploads
// it to this view's FRAME-frequency uniform buffer. Create does not call
// this itself (a caller may want to set up several views before touching
// the RHI for any one of them) -- call at least once before a draw using
// this view, and again whenever viewMatrix/projectionMatrix change.
void Fluxion_RenderView_UpdateFrameConstants(FluxionRenderViewHandle view);

// How many shadows one view may hold at once. A budget rather than a
// limit on lights: a light that finds no room casts none, which is said
// rather than silently done -- see ShadowAtlas.h.
//
// As many as the atlas has tiles, so this is never the thing that runs
// out first -- the atlas is, and the atlas is the one that can say which
// light lost its shadow.
#define FLUXION_RENDER_VIEW_MAX_SHADOWS 16

// One shadow: a light, a matrix, and how far out it is the one to read.
typedef struct FluxionRenderViewShadow
{
    // What takes the world into this light's clip space. See
    // ShadowMatrices.h for where each kind of light's matrix comes from.
    FluxionMat4 lightViewProjection;

    // Which light in the last SetLights list this shadows.
    u32 lightIndex;

    // How far from the eye this one covers. A light with several -- the
    // sun's cascades -- gives each a larger distance than the last, and
    // a surface reads the nearest one that reaches it. Beyond the largest
    // a light casts no shadow at all, which is what makes a cascade
    // scheme affordable rather than a shadow map the size of the world.
    f32 coverTo;

    // How wide the handover to the next one is, in the same distance.
    // Zero is a hard change, which shows up as a line across the ground.
    f32 blendBand;

    // Along the light's own axis, and along the surface normal. Both undo
    // the same thing from different sides -- a depth recorded at one
    // resolution and tested against a surface sampled at another has the
    // surface shadowing itself in stripes.
    f32 depthBias;
    f32 normalBias;

    // True when this light's shadows are the six faces of a cube around
    // it rather than slices of the distance from the eye.
    //
    // A point light shines every way at once, so which of its maps a
    // surface reads is decided by WHERE THE SURFACE IS relative to the
    // light, not by how far the eye is. Six of them, in the order
    // Fluxion_ShadowMatrices_PointFace numbers them, and `coverTo` then
    // says how far out the whole cube is worth reading at all.
    bool cubeFaces;
} FluxionRenderViewShadow;

// The shadows this view draws, replacing whatever it had.
//
// Copied, and each is given a tile of the atlas here rather than at draw
// time -- so a caller learns at once, from the returned count, how many
// of them there was room for. One light's shadows must be next to each
// other in the array and ordered near to far; that is what lets a surface
// find the sharpest one covering it without searching the whole list.
//
// A LIGHT GETS ALL OF ITS SHADOWS OR NONE. Half a cube is not a worse
// shadow, it is a light that goes dark in three directions, and half a
// cascade set is a shadow that ends somewhere nobody chose. So the
// atlas is asked for each light's shadows together, and a light that
// does not fit is left out whole -- which the returned count says.
//
// Passing zero is not a special case: a scene where nothing casts a
// shadow is a picture rather than a fault, and the pass then does nothing
// rather than drawing an empty atlas.
u32 Fluxion_RenderView_SetShadows(FluxionRenderViewHandle view, const FluxionRenderViewShadow* shadows, u32 count);

// How big this view's shadow atlas is, and how big one tile of it is,
// both in texels. Asked rather than assumed: a caller that imports the
// atlas into a render graph, or reads it back, needs the number the
// engine actually used -- which is what the description asked for, or
// the default when it asked for nothing.
void Fluxion_RenderView_GetShadowAtlasSize(FluxionRenderViewHandle view, u32* outAtlasSize, u32* outTileSize);

// The atlas itself, to import into a render graph under the name the
// shadow pass writes and the forward pass reads -- see
// FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE below. A caller that draws
// through a graph has to import it: the two passes name it, and a name
// nothing stands behind is a shadow nobody drew.
FluxionRHITextureHandle Fluxion_RenderView_GetShadowAtlasTexture(FluxionRenderViewHandle view);

// What both passes call it. Said once, here, rather than spelled out at
// each end -- the two spellings agreeing is the whole of whether a
// shadow drawn is a shadow read.
#define FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE "ShadowPass.Atlas"

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

// What the world looks like in every direction: a cube map and its
// sampler. An invalid view puts back the engine's small black cube -- a
// shader cannot ask whether a texture is bound, and a backend handed an
// empty slot refuses the whole group. `intensity` multiplies both what
// is seen of the sky and the light it casts: one number, because a sky
// brighter than it lights does not match its own reflections.
// HANDS THE VIEW WHAT SOMETHING ELSE MEASURED ABOUT HOW MUCH SKY REACHES
// EACH PIXEL.
//
// A view cannot work this out: it is a screen-space answer, produced by a
// pass that reads the depth and normals of the frame this view describes.
// So the renderer produces it and hands it over here, once a frame.
//
// An invalid view, or `measured` false, puts back the white texture the
// view started with -- which reads as "all of it" and multiplies nothing
// away. There is no state in which the binding is empty.
void Fluxion_RenderView_SetAmbientOcclusion(FluxionRenderViewHandle view, FluxionRHITextureViewHandle occlusion, bool measured);

void Fluxion_RenderView_SetEnvironment(FluxionRenderViewHandle view, FluxionRHITextureViewHandle cubeView,
                                       FluxionRHISamplerHandle sampler, f32 intensity);

// Records the copies that put the lights and the shadows where the GPU
// can read them.
//
// Separate from the setters, and not hidden inside them, because it needs
// a command list and they do not. The buffers a shader reads live in
// memory only the GPU can see -- a buffer the CPU can write cannot also
// be one this backend's structured views are allowed to describe -- so
// there is a copy, and a copy is a recorded command.
//
// Call it inside a recording, before anything that draws with this view.
void Fluxion_RenderView_UploadLighting(FluxionRenderViewHandle view, FluxionRHICommandListHandle commandList);

#ifdef __cplusplus
}
#endif
