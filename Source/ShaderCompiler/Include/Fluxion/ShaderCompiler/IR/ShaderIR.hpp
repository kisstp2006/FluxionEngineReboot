#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <optional>
#include <vector>

namespace Fluxion::ShaderCompiler
{

enum class ShaderStage { Vertex, Fragment, Compute };

// An opaque (texture) resource always occupies two descriptor bindings
// within its group's set -- one FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE
// (at `binding`) and one FLUXION_RHI_BINDING_TYPE_SAMPLER (at
// `samplerBinding`) -- since the portable RHI's binding model (see
// RHI.h) keeps those as two separate binding types rather than a single
// combined-image-sampler descriptor.
struct IRResourceBinding
{
    std::string name;
    ShaderType type;       // Sampler2D or SamplerCube for opaque resources; Float for a storage buffer
    BindingGroup group = BindingGroup::Material;
    int binding = 0;
    // Unused (left at 0) for a storage-buffer entry -- a storage buffer
    // is a single read-write binding, not a texture+sampler pair.
    int samplerBinding = 0;
};

struct IRStageIOField
{
    std::string name;
    ShaderType type;
    int location = 0;
};

struct IROutputSlot
{
    std::string name;
    ShaderType type;
    int slot = 0;
};

struct IRUniformBufferMember
{
    std::string name;
    ShaderType type;
    unsigned int offset = 0;
};

// One merged constant/uniform buffer per BindingGroup that has at least
// one non-opaque [Uniform] -- always at binding 0 of its group's set (an
// opaque resource in the same group starts numbering from 1 instead, see
// BuildIR), since a v1 with a single buffer per frequency is simpler
// than letting a shader author split one group across several buffers
// without anything yet needing that.
struct IRUniformBufferBinding
{
    BindingGroup group = BindingGroup::Material;
    std::vector<IRUniformBufferMember> members;
    unsigned int size = 0; // total byte size (each member on its own 16-byte slot)
};

// The Shader IR is the type-checked Program (its expressions/statements
// are walked directly by the text-emitting backends) plus this
// module-level, resolved metadata: stage IO locations, and per-
// BindingGroup resource/uniform-buffer bindings -- the parts a backend
// can't recover just by re-reading the AST, because it's the IR
// builder's job to assign them.
struct ShaderIRModule
{
    ShaderStage stage = ShaderStage::Fragment;
    std::string entryPoint = "main";

    std::vector<IRStageIOField> inputs;
    std::vector<IRStageIOField> outputs;
    std::vector<IROutputSlot> outputSlots;   // from [Target(N)] declarations
    std::vector<IRResourceBinding> resources; // opaque: samplers, grouped
    std::vector<IRUniformBufferBinding> uniformBuffers; // non-opaque uniforms, grouped
    std::vector<IRResourceBinding> storageBuffers; // [Buffer(Group)] declarations, grouped

    // A `return expr;` inside the entry function always targets this
    // slot (the first [Target(N)] declared in the shader), resolved
    // once here so both backends apply the same rule.
    std::optional<std::string> returnTarget;

    // v1 always dispatches a compute shader with a single fixed
    // workgroup size -- there is no [NumThreads(...)] attribute in the
    // language yet, so both backends emit this same fixed value rather
    // than each hardcoding it independently.
    unsigned int localSizeX = 64;
};

struct IRBuildOptions
{
    // Per-group uniform-buffer budget, in bytes -- the RHI backend this
    // shader will ultimately run on owns the real per-binding limit
    // (e.g. a device engine's minUniformBufferOffsetAlignment-driven maximum),
    // so the caller passes it in rather than this module assuming a
    // fixed limit of its own.
    //
    // Two hundred and fifty-six rather than a hundred and twenty-eight:
    // one matrix is sixty-four bytes on its own, and a frame that carries
    // a view-projection alongside a camera position, a light and a couple
    // of camera settings passes the smaller figure without any of it
    // being extravagant. Every backend guarantees at least sixteen
    // kilobytes, so this is nowhere near a hardware limit -- it is a
    // guard against a shader that has quietly grown a hundred parameters.
    unsigned int maxUniformBufferBytesPerGroup = 256;
};

// Builds the module-level IR metadata from an already-Analyze()'d
// Program -- does not re-run semantic analysis and does not mutate the
// AST beyond what Analyze already did.
ShaderIRModule BuildIR(const Program& program, ShaderStage stage, DiagnosticList& diagnostics, const IRBuildOptions& options = {});

} // namespace Fluxion::ShaderCompiler
