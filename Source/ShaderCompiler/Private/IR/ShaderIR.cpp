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

#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

namespace
{

bool IsOpaqueResource(TypeKind kind)
{
    return kind == TypeKind::Sampler2D || kind == TypeKind::SamplerCube || kind == TypeKind::Sampler2DShadow;
}

// Every uniform-buffer member starts on its own 16-byte boundary and
// occupies as many of those as it needs. This wastes space compared to
// tight std140/HLSL packing rules, but is always correct regardless of
// member order or type mix, which matters more than density for a
// handful of small per-group constants.
//
// AS MANY AS IT NEEDS is the part that is easy to get wrong: a matrix is
// four of these, not one. A rule that advanced by a fixed sixteen would
// place the member after a matrix on top of the matrix, and both the
// engine writing this buffer and the shader reading it would then be
// looking at the same bytes for two different things.
constexpr unsigned int kUniformBufferSlotSize = 16;

unsigned int UniformMemberByteSize(const ShaderType& type)
{
    switch (type.kind)
    {
        case TypeKind::Bool:
        case TypeKind::Int:
        case TypeKind::Uint:
        case TypeKind::Float: return 4;
        case TypeKind::Vec2: return 8;
        case TypeKind::Vec3: return 12;
        case TypeKind::Vec4: return 16;

        // Three and four COLUMNS, each of which is padded to sixteen
        // bytes in every layout this compiler emits -- so a three by
        // three matrix costs forty-eight and not thirty-six.
        case TypeKind::Mat3: return 48;
        case TypeKind::Mat4: return 64;

        // A type nothing here sizes. One slot, which is what the rule was
        // before any of this, and is at least never smaller than the
        // member is.
        default: return kUniformBufferSlotSize;
    }
}

unsigned int RoundUpToUniformSlot(unsigned int bytes)
{
    return (bytes + kUniformBufferSlotSize - 1u) / kUniformBufferSlotSize * kUniformBufferSlotSize;
}

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

// Whether a `discard` appears anywhere under this statement.
//
// Only the fragment stage has pixels to drop. A vertex shader saying it
// is a real error, and one that reached a backend would come back as a
// message about generated text nobody wrote -- so it is caught here,
// where the stage is known and the source location still is too.
static const Stmt* FindDiscard(const Stmt& stmt)
{
    switch (stmt.kind)
    {
        case StmtKind::Discard:
            return &stmt;

        case StmtKind::Block: {
            const auto& block = static_cast<const BlockStmt&>(stmt);
            for (const StmtPtr& inner : block.statements)
            {
                if (const Stmt* found = FindDiscard(*inner)) return found;
            }
            return nullptr;
        }

        case StmtKind::If: {
            const auto& branch = static_cast<const IfStmt&>(stmt);
            if (branch.thenBranch)
            {
                if (const Stmt* found = FindDiscard(*branch.thenBranch)) return found;
            }
            if (branch.elseBranch) return FindDiscard(*branch.elseBranch);
            return nullptr;
        }

        case StmtKind::For: {
            const auto& loop = static_cast<const ForStmt&>(stmt);
            return loop.body ? FindDiscard(*loop.body) : nullptr;
        }

        case StmtKind::While: {
            const auto& loop = static_cast<const WhileStmt&>(stmt);
            return loop.body ? FindDiscard(*loop.body) : nullptr;
        }

        default:
            return nullptr;
    }
}

ShaderIRModule BuildIR(const Program& program, ShaderStage stage, DiagnosticList& diagnostics, const IRBuildOptions& options)
{
    ShaderIRModule module;
    module.stage = stage;

    if (stage != ShaderStage::Fragment)
    {
        for (const DeclPtr& decl : program.declarations)
        {
            if (decl->kind != DeclKind::Function) continue;
            const auto* function = static_cast<const FunctionDecl*>(decl.get());
            if (!function->body) continue;

            if (const Stmt* found = FindDiscard(*function->body))
            {
                diagnostics.AddError(found->location,
                    "'discard' only means something in a fragment shader -- there are no pixels to drop in any other stage");
            }
        }
    }

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
                    group.nextMemberOffset += RoundUpToUniformSlot(UniformMemberByteSize(d->type));
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
