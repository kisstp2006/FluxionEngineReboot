#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <optional>
#include <vector>

namespace Fluxion::ShaderCompiler
{

enum class ShaderStage { Vertex, Fragment, Compute };

// The logical binding groups a future descriptor-set-aware pipeline
// layout will place resources into -- recorded now so the backends and
// reflection output already carry this metadata, even though today's
// pipeline layout only has a single push-constant range.
enum class BindingGroup { Global, Frame, Material, Object };

struct IRResourceBinding
{
    std::string name;
    ShaderType type;       // Sampler2D or SamplerCube for opaque resources
    BindingGroup group = BindingGroup::Material;
    int slot = 0;           // opaque-resource register/binding slot
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

struct IRPushConstantMember
{
    std::string name;
    ShaderType type;
    unsigned int offset = 0;
};

// The Shader IR is the type-checked Program (its expressions/statements
// are walked directly by the text-emitting backends) plus this
// module-level, resolved metadata: stage IO locations, opaque-resource
// binding slots, and push-constant layout -- the parts a backend can't
// recover just by re-reading the AST, because it's the IR builder's job
// to assign them.
struct ShaderIRModule
{
    ShaderStage stage = ShaderStage::Fragment;
    std::string entryPoint = "main";

    std::vector<IRStageIOField> inputs;
    std::vector<IRStageIOField> outputs;
    std::vector<IROutputSlot> outputSlots;   // from [Target(N)] declarations
    std::vector<IRResourceBinding> resources; // opaque: samplers
    std::vector<IRPushConstantMember> pushConstants; // everything else uniform

    // A `return expr;` inside the entry function always targets this
    // slot (the first [Target(N)] declared in the shader), resolved
    // once here so both backends apply the same rule.
    std::optional<std::string> returnTarget;
};

struct IRBuildOptions
{
    // The pipeline layout's actual push-constant budget, in bytes -- the
    // RHI backend this shader will ultimately run on owns the real
    // number (its current Vulkan pipeline layout happens to allocate
    // 128), so the caller passes it in rather than this module assuming
    // a fixed limit of its own.
    unsigned int maxPushConstantBytes = 128;
};

// Builds the module-level IR metadata from an already-Analyze()'d
// Program -- does not re-run semantic analysis and does not mutate the
// AST beyond what Analyze already did.
ShaderIRModule BuildIR(const Program& program, ShaderStage stage, DiagnosticList& diagnostics, const IRBuildOptions& options = {});

} // namespace Fluxion::ShaderCompiler
