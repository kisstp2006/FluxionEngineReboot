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

#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#ifdef __cplusplus
extern "C" {
#endif

// A MATERIAL AS SOMETHING A SCENE CAN POINT AT.
//
// A component that says what an object is made of has to name the
// material by id -- a handle would come back pointing at whatever
// occupied that slot in the next run. That is what this type is for: the
// same material the renderer already builds, with an identity that
// survives being written down.
//
// What a cooked material holds is its SOURCE and its PARAMETER VALUES.
// The source, because a material in this engine is a shader function and
// there is no shader asset type to point at yet; the values, because they
// are what makes two materials sharing one source different materials.
//
// TEXTURES ARE NOT IN HERE. A texture is another asset, and a reference
// from inside an asset to another one needs the loader to resolve
// dependencies before it can finish -- machinery this build does not
// have. Until it does, a texture is set on the material after it loads,
// by whoever loaded it. Said here rather than discovered from a material
// that comes back untextured.

#define FLUXION_MATERIAL_ASSET_TYPE_NAME "Material"

// "FLXS", for surface -- which is what a material in this engine
// declares. "FLXM" was already the mesh's.
#define FLUXION_MATERIAL_ASSET_MAGIC          0x464C5853u
#define FLUXION_MATERIAL_ASSET_FORMAT_VERSION 1

#define FLUXION_MATERIAL_ASSET_MAX_PARAMETERS      32
#define FLUXION_MATERIAL_ASSET_MAX_PARAMETER_NAME  63

typedef enum FluxionMaterialAssetParameterKind
{
    FLUXION_MATERIAL_ASSET_PARAMETER_FLOAT = 0,
    FLUXION_MATERIAL_ASSET_PARAMETER_VEC3,
    FLUXION_MATERIAL_ASSET_PARAMETER_VEC4,
} FluxionMaterialAssetParameterKind;

typedef struct FluxionMaterialAssetParameter
{
    // The canonical name the shader declares -- see MaterialParameters.h.
    // A name the material's own shader does not have is refused when the
    // material is finished, not silently kept.
    char name[FLUXION_MATERIAL_ASSET_MAX_PARAMETER_NAME + 1];

    FluxionMaterialAssetParameterKind kind;

    // Four floats whatever the kind is: a value that is smaller simply
    // leaves the rest alone, and one shape means one piece of code
    // reading it.
    FluxionVec4 value;
} FluxionMaterialAssetParameter;

// What a cooked material is made of on the way in -- the same struct
// describes what to write and what was read, so the two cannot come to
// describe different things.
typedef struct FluxionMaterialAssetData
{
    // The material's own source: an EvaluateSurface function and whatever
    // it needs. The engine appends the pass around it.
    const char* source;

    const FluxionMaterialAssetParameter* parameters;
    u32 parameterCount;
} FluxionMaterialAssetData;

// What a loaded material is, and what the asset system hands back for one.
typedef struct FluxionMaterialAsset
{
    // Valid once the asset is ready. Before the finishing step there is
    // no material, because nothing has been given to a device yet.
    FluxionMaterialHandle material;

    // The pipeline this material is drawn with, built beside it against
    // the formats it was registered for.
    FluxionRenderPipelineHandle pipeline;

    // The compiled shader both of those were made from. Owned here
    // because it was compiled here -- and a material that let it go
    // would leave a device holding shader modules nobody can name.
    FluxionShaderProgramHandle program;

    // The source, until the material is built from it. NULL afterwards --
    // a device holds the compiled shader by then, and a field that is
    // sometimes there and sometimes not is better said than hidden.
    const char* source;

    FluxionMaterialAssetParameter parameters[FLUXION_MATERIAL_ASSET_MAX_PARAMETERS];
    u32 parameterCount;
} FluxionMaterialAsset;

bool Fluxion_MaterialAsset_Write(FluxionStream* stream, const FluxionMaterialAssetData* data);

// Reads the cooked form, with no device involved -- so it can happen on
// a worker, like every other type's load half.
bool Fluxion_MaterialAsset_Read(const u8* bytes, usize size, FluxionMaterialAsset** outAsset);
void Fluxion_MaterialAsset_Destroy(FluxionMaterialAsset* asset);

FluxionAssetTypeId Fluxion_MaterialAsset_TypeId(void);

// Registers the material type with the asset system.
//
// The formats are here for the same reason the device is: a pipeline is
// built against the attachments it will be drawn into, and nothing
// reachable from a loaded material can find that out. They are what every
// material loaded through this type is built for.
bool Fluxion_MaterialAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                        FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat);
void Fluxion_MaterialAsset_UnregisterType(void);

#ifdef __cplusplus
}
#endif
