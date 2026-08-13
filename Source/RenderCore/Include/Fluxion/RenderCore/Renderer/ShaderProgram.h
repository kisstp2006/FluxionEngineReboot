#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_HANDLE(FluxionShaderProgramHandle);

// Exactly one of the two shader-stage pairs below is set: either
// (vertexSource + fragmentSource) for a graphics program, or
// (computeSource) for a compute program -- never both, never neither.
// Source is this engine's own shading language text (see
// Samples/ForwardRendererDemo/Shaders/*.jsl for real examples), compiled
// and reflected through Fluxion::ShaderCompiler at Create time; entry
// point names default to "main" when left NULL, matching every .jsl
// source's own `void main()` convention.
typedef struct FluxionShaderProgramDesc
{
    const char* debugName; // optional, may be NULL

    const char* vertexSource;
    const char* vertexEntryPoint;
    const char* fragmentSource;
    const char* fragmentEntryPoint;

    const char* computeSource;
    const char* computeEntryPoint;
} FluxionShaderProgramDesc;

// Returns an invalid handle on a source compile error or a shader-stage
// creation failure (logged via FLUXION_LOG_ERROR either way) -- never
// asserts on a bad shader source, only on a malformed desc (both/neither
// stage pair set).
FluxionShaderProgramHandle Fluxion_ShaderProgram_Create(FluxionRHIDeviceHandle device, const FluxionShaderProgramDesc* desc);
void Fluxion_ShaderProgram_Destroy(FluxionShaderProgramHandle program);

#ifdef __cplusplus
}
#endif
