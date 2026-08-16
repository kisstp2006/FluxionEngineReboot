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
