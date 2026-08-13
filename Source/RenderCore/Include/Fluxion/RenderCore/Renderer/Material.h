#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_HANDLE(FluxionMaterialHandle);

// The parameter set is derived once here from `program`'s own reflected
// MATERIAL-group uniform members/textures -- see ShaderIR.hpp's
// IRUniformBufferBinding/IRResourceBinding -- and never changes for this
// material's lifetime, even if the underlying FluxionShaderProgramHandle
// is later destroyed (Material holds its own copy of the layout, not a
// reference back into the program).
FluxionMaterialHandle Fluxion_Material_Create(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program);
void Fluxion_Material_Destroy(FluxionMaterialHandle material);

// Each setter does a linear name scan against the material's derived
// parameter layout -- false (no-op) if `name` isn't a MATERIAL-group
// uniform member of a matching kind (or a MATERIAL-group texture, for
// SetTexture) on this material's shader program. Writes into a CPU-side
// backing buffer and marks it dirty; nothing reaches the RHI until
// Fluxion_Material_FlushDirty.
bool Fluxion_Material_SetFloat(FluxionMaterialHandle material, const char* name, f32 value);
bool Fluxion_Material_SetVec3(FluxionMaterialHandle material, const char* name, FluxionVec3 value);
bool Fluxion_Material_SetVec4(FluxionMaterialHandle material, const char* name, FluxionVec4 value);
bool Fluxion_Material_SetTexture(FluxionMaterialHandle material, const char* name, FluxionRHITextureViewHandle view, FluxionRHISamplerHandle sampler);

// Uploads the CPU-side parameter buffer to this material's own RHI
// buffer and rebuilds its MATERIAL-frequency bind group -- a no-op if
// nothing has changed since the last flush. A draw using this material
// only picks up Set* calls made before the flush that precedes it.
void Fluxion_Material_FlushDirty(FluxionMaterialHandle material);

#ifdef __cplusplus
}
#endif
