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
