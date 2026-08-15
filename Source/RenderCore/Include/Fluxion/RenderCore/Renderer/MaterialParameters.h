#pragma once

#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>

#ifdef __cplusplus
extern "C" {
#endif

// The parameters the engine itself understands.
//
// Material.h can set any parameter a shader declares, by name. That is
// enough to draw with and not enough to reason with: naming a parameter
// at the call site means the caller knows what the shader called it, and
// the engine does not know anything.
//
// These are the ones the engine does know. Each is a fixed name with a
// fixed meaning and a fixed kind, so that the renderer can ask a material
// what colour it is without being told where to look -- which is what
// lighting, sorting and packaging all need.
//
// A material declares as many or as few as it likes. What it may not do
// is declare one of these names as something else: see
// Fluxion_Material_Create's refusal below.

typedef enum FluxionMaterialParameter
{
    // Vector4: the colour in rgb, the opacity in a. One value because it
    // is authored and stored as one.
    FLUXION_MATERIAL_PARAM_BASE_COLOR = 0,

    FLUXION_MATERIAL_PARAM_METALLIC,  // float
    FLUXION_MATERIAL_PARAM_ROUGHNESS, // float, perceptual

    // float. What a non-metal reflects head-on, before the squaring that
    // turns it into a Fresnel value. Without it every non-metal reflects
    // identically.
    FLUXION_MATERIAL_PARAM_REFLECTANCE,

    FLUXION_MATERIAL_PARAM_EMISSIVE,           // Vector3
    FLUXION_MATERIAL_PARAM_NORMAL_SCALE,       // float
    FLUXION_MATERIAL_PARAM_OCCLUSION_STRENGTH, // float

    // float. Only means anything under the masked alpha mode.
    FLUXION_MATERIAL_PARAM_ALPHA_CUTOFF,

    FLUXION_MATERIAL_PARAM_COUNT,
} FluxionMaterialParameter;

typedef enum FluxionMaterialTextureSlot
{
    FLUXION_MATERIAL_TEXTURE_BASE_COLOR = 0,
    FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
    FLUXION_MATERIAL_TEXTURE_NORMAL,
    FLUXION_MATERIAL_TEXTURE_OCCLUSION,
    FLUXION_MATERIAL_TEXTURE_EMISSIVE,

    FLUXION_MATERIAL_TEXTURE_COUNT,
} FluxionMaterialTextureSlot;

// How a surface's opacity is meant to be dealt with.
//
// This is not a shader parameter and cannot be one: it decides which pass
// a draw belongs to and what blend state the pipeline is built with, and
// both of those are settled before any shader runs. So it lives on the
// material, on this side.
typedef enum FluxionMaterialAlphaMode
{
    // The opacity is ignored entirely.
    FLUXION_MATERIAL_ALPHA_OPAQUE = 0,

    // A pixel is either there or it is not, decided against the alpha
    // cutoff. Still an opaque draw as far as sorting is concerned.
    FLUXION_MATERIAL_ALPHA_MASK,

    // Blended, and therefore drawn after everything opaque, back to
    // front.
    FLUXION_MATERIAL_ALPHA_BLEND,
} FluxionMaterialAlphaMode;

// What each one is, as a value rather than as a comment.
//
// A caller listing a material's parameters needs this, and so does
// anything checking that the shader library and this header still agree:
// the name alone is not the agreement, the name AND the kind are. A base
// colour declared as a single number would be found by name and then
// written four times its own size.
typedef enum FluxionMaterialParameterType
{
    FLUXION_MATERIAL_PARAM_TYPE_FLOAT = 0,
    FLUXION_MATERIAL_PARAM_TYPE_VEC3,
    FLUXION_MATERIAL_PARAM_TYPE_VEC4,
} FluxionMaterialParameterType;

// Float for a value out of range, which is the narrowest answer and
// therefore the one least likely to be written over something.
FluxionMaterialParameterType Fluxion_Material_GetParameterType(FluxionMaterialParameter parameter);

// The name each one is declared under in a shader. Never NULL for a value
// in range; NULL otherwise.
//
// These strings are the whole of the agreement between this header and
// Fluxion/Material.jsl. Nothing else connects the two, which is why a
// test compiles that file and checks that every name below is found in
// what the shader reflected.
const char* Fluxion_Material_GetParameterName(FluxionMaterialParameter parameter);
const char* Fluxion_Material_GetTextureSlotName(FluxionMaterialTextureSlot slot);

// Whether this material's shader declares it. A material using a subset
// is perfectly ordinary -- one with no emissive does not declare an
// emissive factor, and asking to set it answers false rather than writing
// somewhere it should not.
bool Fluxion_Material_HasParameter(FluxionMaterialHandle material, FluxionMaterialParameter parameter);
bool Fluxion_Material_HasTextureSlot(FluxionMaterialHandle material, FluxionMaterialTextureSlot slot);

// Setters that name the parameter rather than spelling it.
//
// False when this material does not declare that parameter. The kind
// cannot be wrong: a material declaring one of these names with a
// different kind is refused when it is created.
bool Fluxion_Material_SetBaseColor(FluxionMaterialHandle material, FluxionVec4 baseColor);
bool Fluxion_Material_SetMetallic(FluxionMaterialHandle material, f32 metallic);
bool Fluxion_Material_SetRoughness(FluxionMaterialHandle material, f32 perceptualRoughness);
bool Fluxion_Material_SetReflectance(FluxionMaterialHandle material, f32 reflectance);
bool Fluxion_Material_SetEmissive(FluxionMaterialHandle material, FluxionVec3 emissive);
bool Fluxion_Material_SetNormalScale(FluxionMaterialHandle material, f32 normalScale);
bool Fluxion_Material_SetOcclusionStrength(FluxionMaterialHandle material, f32 occlusionStrength);
bool Fluxion_Material_SetAlphaCutoff(FluxionMaterialHandle material, f32 alphaCutoff);

bool Fluxion_Material_SetTextureSlot(FluxionMaterialHandle material, FluxionMaterialTextureSlot slot,
                                     FluxionRHITextureViewHandle view, FluxionRHISamplerHandle sampler);

// Opaque until told otherwise, which is what almost everything is and
// what costs least to draw.
void Fluxion_Material_SetAlphaMode(FluxionMaterialHandle material, FluxionMaterialAlphaMode mode);
FluxionMaterialAlphaMode Fluxion_Material_GetAlphaMode(FluxionMaterialHandle material);

#ifdef __cplusplus
}
#endif
