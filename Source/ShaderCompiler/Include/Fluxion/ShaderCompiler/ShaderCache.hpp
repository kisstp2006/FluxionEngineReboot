#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

// Turning one shader source into something a device can be handed is the
// slowest thing that happens on the way to a first frame, and almost every
// run does it to exactly the sources the last run did. This keeps the
// finished result beside the source it came from.
//
// What is kept is deliberately the *finished* result -- the bytes a device
// takes, plus the reflection that describes them -- and not the
// intermediate text. Most of the time is spent past that intermediate
// step, in the external tool that turns it into bytes, so a cache that
// stopped there would save the smaller half.

// Which of the three shapes a device can be handed is wanted. Named here
// in the compiler's own terms rather than by graphics API, because the
// compiler has no business knowing which backends exist -- the caller
// that does know maps its own choice onto this.
enum class ArtifactTarget
{
    Spirv,
    Dxil,
    Glsl,
};

struct ShaderCacheOptions
{
    // Where results are kept. Created if it is not there. Empty turns the
    // whole thing off: every lookup misses and nothing is written, so a
    // caller that does not want a cache does not need a different call.
    std::string directory;

    // Leave whatever is on disk alone. A hit still answers out of it; a
    // miss still compiles. For a caller reading a cache it did not build
    // and must not disturb.
    bool readOnly = false;
};

struct ShaderCacheReport
{
    // True when the result came out of a file rather than the compiler.
    bool wasCached = false;

    // True when this call put a result on disk. False on a hit, when the
    // cache is off or read-only, and when the file could not be written --
    // none of which is an error.
    bool wrote = false;

    // The file this call looked in, empty when the cache is off.
    std::string path;
};

struct CompiledArtifact
{
    ShaderIRModule reflection;

    // What the device is handed: SPIR-V or DXIL bytes, or GLSL source
    // text as bytes. Which one is whatever target was asked for -- the
    // caller knows, having asked.
    std::vector<u8> bytes;
};

// Everything that decides the answer, and nothing that does not.
//
// The two option structs are carried whole rather than picked apart,
// because a field added to either of them is a field that changes the
// output, and a key that had to be taught about it separately is a key
// that would go on answering the old way until someone remembered.
struct ArtifactRequest
{
    ArtifactTarget target = ArtifactTarget::Spirv;
    CompileOptions compile;

    // Only the one matching the target is consulted, but both travel
    // together so a caller need not know which.
    DXCOptions spirv;
    DXILOptions dxil;
};

// Produces the artifact, out of the cache when an identical request has
// been answered before. Diagnostics are the compiler's own: a hit produces
// none, because nothing was compiled -- including warnings an earlier
// compilation reported, which do not come back with the result.
Fluxion::Foundation::Result<CompiledArtifact> CompileArtifactCached(const std::string& source, const ArtifactRequest& request,
    const ShaderCacheOptions& cache, DiagnosticList& outDiagnostics, ShaderCacheReport& outReport);

// How many artifacts this process actually built through the call above,
// and how many it read from a file instead. Both only go up. "The cache
// is working" is otherwise only visible as the run being quicker, which
// proves nothing on a busy machine -- these are what a test asserts on.
//
// Counted atomically, because compilation is meant to be movable onto a
// worker and a counter that only tells the truth on one thread would stop
// being evidence the moment it was.
struct ShaderCacheCounters
{
    u64 compiled = 0;
    u64 loaded = 0;
};

ShaderCacheCounters GetShaderCacheCounters();

} // namespace Fluxion::ShaderCompiler
