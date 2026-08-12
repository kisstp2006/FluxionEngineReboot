#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

namespace
{

bool IsOpaqueResource(TypeKind kind) { return kind == TypeKind::Sampler2D || kind == TypeKind::SamplerCube; }

// Every uniform-buffer member is placed on its own 16-byte slot. This
// wastes space compared to tight std140/HLSL packing rules, but is
// always correct regardless of member order or type mix, which matters
// more than density for a handful of small per-group constants.
constexpr unsigned int kUniformBufferSlotSize = 16;

constexpr size_t kBindingGroupCount = 4; // Global, Frame, Material, Object

// Per-BindingGroup working state while walking the declaration list --
// a texture/sampler pair's binding is only final once it's known whether
// its group also has a merged uniform buffer (which always claims
// binding 0), so opaque resources are collected here and numbered in a
// second pass rather than assigned a binding as they're seen.
struct GroupState
{
    std::vector<IRUniformBufferMember> members;
    unsigned int nextMemberOffset = 0;
    std::vector<std::pair<std::string, ShaderType>> opaqueResources; // declaration order
    std::vector<std::pair<std::string, ShaderType>> storageBufferResources; // declaration order, from [Buffer(Group)]
};

} // namespace

ShaderIRModule BuildIR(const Program& program, ShaderStage stage, DiagnosticList& diagnostics, const IRBuildOptions& options)
{
    ShaderIRModule module;
    module.stage = stage;

    int nextInputLocation = 0;
    int nextOutputLocation = 0;
    GroupState groups[kBindingGroupCount];

    for (const DeclPtr& decl : program.declarations)
    {
        switch (decl->kind)
        {
            case DeclKind::StageIO: {
                auto* d = static_cast<StageIODecl*>(decl.get());
                if (d->direction == StageIODirection::In)
                    module.inputs.push_back(IRStageIOField{ d->name, d->type, nextInputLocation++ });
                else
                    module.outputs.push_back(IRStageIOField{ d->name, d->type, nextOutputLocation++ });
                break;
            }
            case DeclKind::OutputSlot: {
                auto* d = static_cast<OutputSlotDecl*>(decl.get());
                module.outputSlots.push_back(IROutputSlot{ d->name, d->type, d->slot });
                if (!module.returnTarget.has_value())
                    module.returnTarget = d->name;
                break;
            }
            case DeclKind::Uniform: {
                auto* d = static_cast<UniformDecl*>(decl.get());
                GroupState& group = groups[(size_t)d->group];
                if (d->isStorageBuffer)
                {
                    group.storageBufferResources.emplace_back(d->name, d->type);
                }
                else if (IsOpaqueResource(d->type.kind))
                {
                    group.opaqueResources.emplace_back(d->name, d->type);
                }
                else
                {
                    group.members.push_back(IRUniformBufferMember{ d->name, d->type, group.nextMemberOffset });
                    group.nextMemberOffset += kUniformBufferSlotSize;
                }
                break;
            }
            default:
                break; // Function/GlobalConst/Struct don't contribute module-level binding metadata.
        }
    }

    for (size_t g = 0; g < kBindingGroupCount; ++g)
    {
        GroupState& group = groups[g];
        BindingGroup groupEnum = (BindingGroup)g;

        bool hasUniformBuffer = !group.members.empty();
        if (hasUniformBuffer)
        {
            if (group.nextMemberOffset > options.maxUniformBufferBytesPerGroup)
                diagnostics.AddError(SourceLocation{}, "a BindingGroup's uniform buffer uses more than " + std::to_string(options.maxUniformBufferBytesPerGroup) + " bytes (the configured per-group limit)");
            module.uniformBuffers.push_back(IRUniformBufferBinding{ groupEnum, std::move(group.members), group.nextMemberOffset });
        }

        int nextBinding = hasUniformBuffer ? 1 : 0; // binding 0 is reserved for the group's uniform buffer, if it has one
        for (auto& [name, type] : group.opaqueResources)
        {
            int textureBinding = nextBinding++;
            int samplerBinding = nextBinding++;
            module.resources.push_back(IRResourceBinding{ name, type, groupEnum, textureBinding, samplerBinding });
        }
        // A storage buffer takes exactly one binding number (no paired
        // sampler binding), unlike an opaque texture resource above.
        for (auto& [name, type] : group.storageBufferResources)
        {
            int binding = nextBinding++;
            module.storageBuffers.push_back(IRResourceBinding{ name, type, groupEnum, binding, 0 });
        }
    }

    return module;
}

} // namespace Fluxion::ShaderCompiler
