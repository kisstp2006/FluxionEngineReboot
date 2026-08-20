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

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

#ifdef __cplusplus
extern "C" {
#endif

// WHICH WAY A FRAME IS RENDERED, CHOSEN IN DATA.
//
// A render pipeline asset is policy: which graph draws the frame, and the
// handful of settings that are not the graph's own business. It sits one
// step above RenderGraphAsset --
//
//   RenderPipelineAsset -> RenderGraphAsset -> render graph -> RHI
//
// -- and the reason for the separation is that two projects can want the
// same pass topology at different quality, and one project can want two
// topologies at the same quality.
//
// NOT TO BE CONFUSED WITH FluxionRenderPipelineHandle (Renderer/
// RenderPipeline.h), which is a GPU pipeline state object: one is what a
// draw is compiled into, this one is what a frame is planned by. The
// include path says which is meant.
//
// SETTINGS THIS BUILD CANNOT HONOUR ARE REFUSED, NOT IGNORED. Every knob
// the design calls for is spelled here and understood by the reader, but
// the ones with no pass behind them yet may only be switched off, and
// asking for one by name fails the parse and says which. A setting that
// was accepted and quietly did nothing would make a file that lies about
// the picture it produces.

#define FLUXION_RENDER_PIPELINE_ASSET_TYPE_NAME "RenderPipeline"

#define FLUXION_RENDER_PIPELINE_ASSET_MAGIC          0x464C5850u // "FLXP"
#define FLUXION_RENDER_PIPELINE_ASSET_FORMAT_VERSION 3

#define FLUXION_RENDER_PIPELINE_ASSET_MAX_NAME_LENGTH 63

typedef enum FluxionRenderPipelineLighting
{
    // Every light evaluated by the surface that is lit by it. What the
    // forward passes in this build do, and so the zero value.
    FLUXION_RENDER_PIPELINE_LIGHTING_FORWARD = 0,

    // Lights assigned to volumes of the view first, and read per volume.
    // NO PASS BUILDS THOSE VOLUMES YET -- naming it fails the parse.
    FLUXION_RENDER_PIPELINE_LIGHTING_CLUSTERED,
} FluxionRenderPipelineLighting;

// How much atlas a frame's shadows get. Not a switch on the shadow pass:
// whether shadows are drawn at all is decided by whether the graph has a
// shadow pass in it, and this decides how sharp they are when it does.
typedef enum FluxionRenderPipelineShadowQuality
{
    // Nothing asked for, and so the zero value. The atlas shrinks to a
    // single small tile -- a graph with no shadow pass never draws into
    // it, and an atlas that is never drawn into reads as "the light got
    // here", which is a lit scene rather than a black one.
    FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF = 0,

    FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_LOW,
    FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_MEDIUM,
    FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH,
} FluxionRenderPipelineShadowQuality;

// WHO DECIDES what a frame can see.
//
// The first setting here that refuses nothing: both answers have a pass
// behind them, and which one is better depends on the scene rather than
// on the build. What differs is only where the arithmetic happens --
// the tests, and the answers they produce, are the same.
typedef enum FluxionRenderPipelineCulling
{
    // On the processor, while the frame is being gathered. The zero
    // value, because it is what a file that never mentions culling gets
    // and what every frame did before there was a choice.
    FLUXION_RENDER_PIPELINE_CULLING_CPU = 0,

    // In a compute pass, just before the frame draws.
    FLUXION_RENDER_PIPELINE_CULLING_GPU,
} FluxionRenderPipelineCulling;

typedef struct FluxionRenderPipelineAssetSettings
{
    FluxionRenderPipelineLighting lighting;
    FluxionRenderPipelineShadowQuality shadowQuality;
    FluxionRenderPipelineCulling culling;

    // WHETHER THE SCENE GOES THROUGH THE CHAIN. See
    // Fluxion_Renderer_SetPostProcessEnabled for what it costs: with it
    // on, everything drawing the scene has to be built for a target of
    // light rather than for the screen.
    bool postfx;

    // WHAT GLOWS SPREADS INTO WHAT IS BESIDE IT. Needs "postfx": the
    // glow is built from the light the scene was drawn in, and there is
    // no such picture without the chain. Asked for without it, it is
    // simply not switched on.
    bool bloom;

    // The three that have no pass in this build. False is the only value
    // a file may give them; true is refused by name.
    bool taa;
    bool ssao;
    bool ssr;

    // Zero and one both mean no multisampling, and nothing else is
    // accepted yet -- the RHI has no multisampled attachment to ask for.
    u32 msaaSamples;
} FluxionRenderPipelineAssetSettings;

typedef struct FluxionRenderPipelineAsset
{
    char name[FLUXION_RENDER_PIPELINE_ASSET_MAX_NAME_LENGTH + 1];

    // An id rather than a name: a name is what a person writes in the
    // authored file, and an id is what survives the folder being
    // reorganised. Turning one into the other is the cook's job, below.
    FluxionAssetRef graph;

    FluxionRenderPipelineAssetSettings settings;
} FluxionRenderPipelineAsset;

// --- The authored form ----------------------------------------------------
//
// A `.pipeline` file:
//
//   {
//     "name": "DefaultForward",
//     "graph": "DefaultForward",
//     "lighting": "forward",
//     "shadowQuality": "high",
//     "culling": "cpu",
//     "taa": false,
//     "ssao": false,
//     "ssr": false,
//     "bloom": false,
//     "msaa": 1
//   }
//
// "graph" is required and everything else is optional, defaulting to the
// zero value of its field.

// Turns the graph's authored name into the id the asset holds. Whoever
// cooks a project knows what is in its database; this file does not.
//
// False means "no such graph", and that fails the whole parse: a pipeline
// pointing at a graph nobody can find is broken, and the cook is the
// cheapest moment to learn it.
typedef bool (*FluxionRenderPipelineGraphResolveFn)(const char* graphName, FluxionUUID* outGraphId, void* context);

// Nothing is written into `outAsset` unless the whole text parsed and
// every setting in it can actually be honoured.
bool Fluxion_RenderPipelineAsset_ParseText(const char* text, usize length, FluxionRenderPipelineGraphResolveFn resolve,
                                           void* context, FluxionRenderPipelineAsset* outAsset);

// Whether this build can do what these settings ask for, on its own --
// the same check the parse makes, exposed because a pipeline asset can
// also be built in code, and one built that way deserves the same answer.
// `outUnsupported`, when not NULL, is left pointing at a static string
// naming the first setting that cannot be honoured.
bool Fluxion_RenderPipelineAsset_AreSettingsSupported(const FluxionRenderPipelineAssetSettings* settings, const char** outUnsupported);

// --- The cooked form ------------------------------------------------------

bool Fluxion_RenderPipelineAsset_Write(FluxionStream* stream, const FluxionRenderPipelineAsset* asset);
bool Fluxion_RenderPipelineAsset_Read(const u8* bytes, usize size, FluxionRenderPipelineAsset** outAsset);
void Fluxion_RenderPipelineAsset_Destroy(FluxionRenderPipelineAsset* asset);

FluxionAssetTypeId Fluxion_RenderPipelineAsset_TypeId(void);
bool Fluxion_RenderPipelineAsset_RegisterType(void);
void Fluxion_RenderPipelineAsset_UnregisterType(void);

// --- What the settings actually do ----------------------------------------

// Fills in the parts of a view description this asset decides -- today
// the shadow atlas and tile sizes its quality level asks for.
//
// Before Fluxion_RenderView_Create, because that is when the atlas is
// made. A program that switches to a pipeline of a different shadow
// quality has to build a new view; the old one's atlas is the size it
// was made.
void Fluxion_RenderPipelineAsset_ApplyToViewDesc(const FluxionRenderPipelineAsset* asset, FluxionRenderViewDesc* desc);

// And the parts a renderer decides rather than a view -- today, who
// works out what the frame can see.
//
// Every frame, not once at start-up: a program may switch pipelines
// while it runs, and this is what makes that switch take effect.
void Fluxion_RenderPipelineAsset_ApplyToRenderer(const FluxionRenderPipelineAsset* asset, FluxionRendererHandle renderer);

// --- Which pipeline a view is drawn with ----------------------------------
//
// Two places may say, and this is the order: what a camera names wins,
// and what the project names is what everything else uses. Nothing else
// gets a say -- a renderer that also had an opinion would be a third
// answer nobody asked for.

void Fluxion_RenderPipelineAsset_SetProjectDefault(FluxionAssetRef pipeline);
FluxionAssetRef Fluxion_RenderPipelineAsset_GetProjectDefault(void);

// The override when it is set, the project default otherwise. A nil
// reference back means neither was set, which a caller can tell apart
// from a reference that points at something missing.
FluxionAssetRef Fluxion_RenderPipelineAsset_Resolve(FluxionAssetRef cameraOverride);

#ifdef __cplusplus
}
#endif
