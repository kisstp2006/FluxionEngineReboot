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

// Where finished shaders are kept so they need not be built again, for
// every program created from here on. NULL or empty turns it off, which
// is the state a program starts in: a host that says nothing gets the
// behaviour it always had.
//
// One directory for the whole process rather than one per program,
// because it is one answer -- every program in a run wants the same
// place, and a per-program field would only be the same string written
// out at every call site. Said once, at startup, by the host that knows
// where its files belong.
//
// The directory itself is created when first written to. Nothing here
// fails because of it: a path that cannot be written to means every
// program is built the long way, and nothing is reported, because from
// the caller's point of view nothing went wrong.
void Fluxion_ShaderProgram_SetCacheDirectory(const char* directory);

// Returns an invalid handle on a source compile error or a shader-stage
// creation failure (logged via FLUXION_LOG_ERROR either way) -- never
// asserts on a bad shader source, only on a malformed desc (both/neither
// stage pair set).
FluxionShaderProgramHandle Fluxion_ShaderProgram_Create(FluxionRHIDeviceHandle device, const FluxionShaderProgramDesc* desc);

// Why a reload did not happen, for a caller that wants to say so.
typedef enum FluxionShaderProgramReloadOutcome
{
    FLUXION_SHADER_PROGRAM_RELOAD_OK = 0,

    // The new source did not compile. Nothing changed; what was running
    // before is still running.
    FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED,

    // The new source compiled but describes a different set of material
    // parameters than the one now in use. Every material built from this
    // program holds byte offsets and a buffer sized from the old set, and
    // there is no honest way to reinterpret their contents against the
    // new one -- so the reload is refused and the old shaders stay live.
    // Changing a shader's material parameters needs a restart.
    FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED,

    // The handle does not name a live program, or the description asks
    // for a different shape (graphics vs compute) than the program has.
    FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST,
} FluxionShaderProgramReloadOutcome;

// Replaces a live program's shaders with freshly compiled ones, keeping
// the handle valid.
//
// Keeping the handle is the whole point: a RenderPipeline and a Material
// each hold this handle, so a destroy-and-recreate would leave both
// pointing at nothing. What changes is what the handle refers to.
//
// Everything derived from the old shaders is rebuilt as part of this --
// every pipeline built from this program is dropped and will be rebuilt
// on next use. Materials are deliberately left alone, which is only safe
// because a reload that would have invalidated them is refused instead
// (see FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED).
//
// Must not be called while a frame is being recorded: it destroys RHI
// objects that the frame in progress may still be about to reference.
// Call it between frames, where the GPU is known to be done.
FluxionShaderProgramReloadOutcome Fluxion_ShaderProgram_Reload(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program,
    const FluxionShaderProgramDesc* desc);

// --- The same reload, with the waiting taken off the frame ---------------
//
// Compiling a shader takes long enough to be seen as a stall, and none of
// it needs the device: it is text going in and bytes coming out. So it is
// handed to a worker, and only the part that must happen on the thread
// that owns the device -- making the shader objects and swapping them in
// -- waits for a moment when that is safe.
//
// The device is never touched off-thread. A worker produces bytes and
// stops; an OpenGL context belongs to one thread and would not tolerate
// anything else.

typedef struct FluxionShaderProgramReloadJob FluxionShaderProgramReloadJob;

// Copies the sources and starts compiling. Returns NULL if the program is
// not live, if the description asks for a different shape, or if the work
// could not be started -- in which case nothing has happened and the
// caller may simply try the direct Fluxion_ShaderProgram_Reload instead.
//
// The sources are copied, so the caller's strings need not outlive this
// call.
FluxionShaderProgramReloadJob* Fluxion_ShaderProgram_BeginReload(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program,
    const FluxionShaderProgramDesc* desc);

// Whether the compiling is finished. Never blocks, so a frame loop can
// ask every frame and carry on when the answer is no.
bool Fluxion_ShaderProgram_IsReloadReady(const FluxionShaderProgramReloadJob* job);

// Applies the finished reload and releases the job, which must not be
// used afterwards. Waits if the compiling is not done yet, so a caller
// that wants to shut down can call it directly rather than spinning.
//
// Same requirement as the direct reload: call it between frames. This is
// where the shader objects are made and the old ones let go.
FluxionShaderProgramReloadOutcome Fluxion_ShaderProgram_FinishReload(FluxionShaderProgramReloadJob* job);
void Fluxion_ShaderProgram_Destroy(FluxionShaderProgramHandle program);

#ifdef __cplusplus
}
#endif
