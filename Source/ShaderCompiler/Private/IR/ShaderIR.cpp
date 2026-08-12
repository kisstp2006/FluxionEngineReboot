#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

namespace
{

bool IsOpaqueResource(TypeKind kind) { return kind == TypeKind::Sampler2D || kind == TypeKind::SamplerCube; }

// Every push-constant member is placed on its own 16-byte slot. This
// wastes space compared to tight std140/HLSL packing rules, but is
// always correct regardless of member order or type mix, which matters
// more than density for a handful of small per-draw constants.
constexpr unsigned int kPushConstantSlotSize = 16;

} // namespace

ShaderIRModule BuildIR(const Program& program, ShaderStage stage, DiagnosticList& diagnostics, const IRBuildOptions& options)
{
    ShaderIRModule module;
    module.stage = stage;

    int nextInputLocation = 0;
    int nextOutputLocation = 0;
    int nextTextureSlot = 0;
    unsigned int nextPushConstantOffset = 0;

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
                if (IsOpaqueResource(d->type.kind))
                {
                    module.resources.push_back(IRResourceBinding{ d->name, d->type, BindingGroup::Material, nextTextureSlot++ });
                }
                else
                {
                    module.pushConstants.push_back(IRPushConstantMember{ d->name, d->type, nextPushConstantOffset });
                    nextPushConstantOffset += kPushConstantSlotSize;
                }
                break;
            }
            default:
                break; // Function/GlobalConst/Struct don't contribute module-level binding metadata.
        }
    }

    if (nextPushConstantOffset > options.maxPushConstantBytes)
        diagnostics.AddError(SourceLocation{}, "shader uses more than " + std::to_string(options.maxPushConstantBytes) + " bytes of push-constant storage (the current pipeline layout's limit)");

    return module;
}

} // namespace Fluxion::ShaderCompiler
