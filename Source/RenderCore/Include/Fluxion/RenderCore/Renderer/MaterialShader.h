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

// Turning one material's source into the shader for one pass.
//
// A material's own source declares what the surface is and stops:
//
//     #include "Fluxion/Surface.jsl"
//     SurfaceData EvaluateSurface() { ... }
//
// It has no entry point, no render target, and no opinion about lighting.
// This adds the part that does, chosen by pass -- so ONE material source
// becomes a forward shader, a depth shader, and later whatever else,
// without a line of it changing.
//
// What gets appended is a single include. Everything real lives in the
// engine's shader library, where the compiler reports it as an include
// and the shader cache therefore knows the result depends on it. Pasting
// the pass code in directly would work and would cost exactly that: the
// cache would not know, and a changed pass would go on serving the shader
// built from the old one.

// The name a material's source must declare, so a caller can say so in an
// error message rather than leaving the author to find out from the
// compiler.
#define FLUXION_MATERIAL_SURFACE_FUNCTION "EvaluateSurface"

typedef enum FluxionMaterialPass
{
    // Lit into one colour, now.
    FLUXION_MATERIAL_PASS_FORWARD = 0,

    // Only whether the pixel is there. Reads the opacity and nothing
    // else -- which is the point: a material that could only be drawn one
    // way could not be read this way at all.
    FLUXION_MATERIAL_PASS_DEPTH_ONLY,

    FLUXION_MATERIAL_PASS_COUNT,
} FluxionMaterialPass;

// The library file that supplies a pass's fragment entry point. NULL for
// a pass outside the range above.
const char* Fluxion_MaterialShader_GetPassInclude(FluxionMaterialPass pass);

// And its vertex entry point.
//
// Today every pass answers with the same file, because a depth pass has
// to put a vertex exactly where the forward pass puts it -- if the two
// disagreed about where the surface is, the depth test would reject the
// wrong pixels and the picture would be wrong in a way that looks like a
// sorting bug. It is a separate question from the fragment one anyway, so
// that the day a pass needs different vertex work it can have it without
// every caller changing.
const char* Fluxion_MaterialShader_GetVertexPassInclude(FluxionMaterialPass pass);

// Builds the fragment source for `pass` from `materialSource`.
//
// NULL on a pass out of range or a null source. What comes back is owned
// by the caller and freed with Fluxion_MaterialShader_FreeSource.
char* Fluxion_MaterialShader_BuildFragmentSource(const char* materialSource, FluxionMaterialPass pass);

// Builds the vertex source for `pass`.
//
// There is no material source in this one, and that is the point: a
// material describes a surface, not where its vertices go. Until moving
// vertices is an ability a material has, this is the engine's shader
// entirely -- and a caller asks for it rather than writing one, so that
// what it produces stays in step with what the fragment side expects to
// receive.
char* Fluxion_MaterialShader_BuildVertexSource(FluxionMaterialPass pass);

void Fluxion_MaterialShader_FreeSource(char* source);

#ifdef __cplusplus
}
#endif
