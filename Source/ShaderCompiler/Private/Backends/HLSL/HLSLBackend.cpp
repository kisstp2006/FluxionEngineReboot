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

#include <Fluxion/ShaderCompiler/Backends/HLSL/HLSLBackend.hpp>

#include <sstream>
#include <unordered_map>

namespace Fluxion::ShaderCompiler
{

namespace
{

std::string HLSLTypeName(const ShaderType& type)
{
    switch (type.kind)
    {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Uint: return "uint";
        case TypeKind::Float: return "float";
        case TypeKind::Vec2: return "float2";
        case TypeKind::Vec3: return "float3";
        case TypeKind::Vec4: return "float4";
        case TypeKind::Mat3: return "float3x3";
        case TypeKind::Mat4: return "float4x4";
        case TypeKind::Sampler2D: return "Texture2D";
        case TypeKind::SamplerCube: return "TextureCube";

        // One channel, said outright: a depth texture has only the one,
        // and a comparison sample against a four-channel declaration is
        // refused here rather than quietly reading three channels that
        // do not exist.
        case TypeKind::Sampler2DShadow: return "Texture2D<float>";

        // A declared struct carries its own name and that is what is
        // emitted. Falling through to the default below would turn every
        // struct-typed thing into a float -- which compiles, and is
        // wrong, which is the worse of the two.
        case TypeKind::Unresolved: return type.structName;
        default: return "float";
    }
}

const char* BinaryOpText(BinaryOp op)
{
    switch (op)
    {
        case BinaryOp::Add: return "+"; case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*"; case BinaryOp::Div: return "/"; case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "=="; case BinaryOp::NotEq: return "!=";
        case BinaryOp::Less: return "<"; case BinaryOp::Greater: return ">";
        case BinaryOp::LessEq: return "<="; case BinaryOp::GreaterEq: return ">=";
        case BinaryOp::And: return "&&"; case BinaryOp::Or: return "||";
        default: return "?";
    }
}

const char* AssignOpText(AssignOp op)
{
    switch (op)
    {
        case AssignOp::Assign: return "="; case AssignOp::AddAssign: return "+=";
        case AssignOp::SubAssign: return "-="; case AssignOp::MulAssign: return "*="; case AssignOp::DivAssign: return "/=";
        default: return "=";
    }
}

std::string RemapCallName(const std::string& name)
{
    static const std::unordered_map<std::string, std::string> table = {
        { "mix", "lerp" }, { "fract", "frac" }, { "inversesqrt", "rsqrt" },
    };
    auto it = table.find(name);
    return it != table.end() ? it->second : name;
}

bool IsTextureSampleCall(const std::string& name) { return name == "texture2D" || name == "texture" || name == "tex2D" || name == "textureCube"; }

// Sampling at a level the shader names, rather than at one worked out from
// how fast the coordinate changes between neighbouring pixels.
//
// It has to be a separate case from the one above because it takes three
// arguments rather than two, and because the two languages disagree about
// it more than usual: `textureLod` IS the name in GLSL, so that backend
// passes it straight through, while here it is a different method on the
// texture object altogether.
//
// A compute shader has no choice but this one. There are no neighbouring
// pixels to compare against in a dispatch, so the ordinary sample has
// nothing to work a level out from.
bool IsTextureSampleLevelCall(const std::string& name) { return name == "textureLod"; }

// The comparison sample. Three arguments like the one above, but the
// third is a depth to compare against rather than a mip level, and what
// comes back is one float -- how much of the filter kernel passed the
// comparison -- rather than a colour. `LevelZero` because a shadow map
// has one level and a fragment shader must not work one out from
// neighbours it may not have.
bool IsTextureCompareCall(const std::string& name) { return name == "textureCompare"; }

// The set index a BindingGroup maps to always equals the enum's integer
// value -- this matches FLUXION_RHI_BIND_GROUP_GLOBAL/FRAME/MATERIAL/
// OBJECT in RHI.h exactly, so a shader compiled with a given group and an
// RHI pipeline built with that same group's bind group layout agree on
// which descriptor set is meant without any separate lookup table.
int GroupSetIndex(BindingGroup group) { return (int)group; }

const char* GroupName(BindingGroup group)
{
    switch (group)
    {
        case BindingGroup::Global: return "Global";
        case BindingGroup::Frame: return "Frame";
        case BindingGroup::Object: return "Object";
        default: return "Material";
    }
}

class HLSLEmitter
{
public:
    HLSLEmitter(const Program& program, const ShaderIRModule& module)
        : m_program(program), m_module(module)
    {
    }

    std::string Run()
    {
        if (m_module.stage == ShaderStage::Compute)
        {
            // Compute has no vertex/fragment-shaped stage-IO struct, so it
            // skips EmitStageStructs()/EmitWrapperMain() (both assume a
            // StageInput/StageOutput/SV_Target/SV_Position shape that
            // doesn't apply here) in favor of a small [numthreads(...)]
            // wrapper that mirrors the same "wrapper calls shaderMain()"
            // idiom used by the vertex/fragment path below.
            // Structs first -- see the vertex/fragment path below for
            // why the order is load-bearing rather than tidy.
            EmitStructs();
            EmitUniformBuffers();
            EmitTextures();
            EmitStorageBuffers();
            EmitComputeStaticMirrors();
            EmitGlobalConsts();
            EmitFunctions();
            EmitComputeWrapperMain();
            return m_out.str();
        }

        // Structs first, before anything that could be made OF one.
        //
        // A storage buffer of light descriptions, say, is declared as
        // StructuredBuffer<LightData> -- and a struct used before it is
        // declared is a compile error in the generated text, reported
        // against a line the shader's author never wrote. This ordering
        // is the only thing preventing that.
        EmitStructs();
        EmitUniformBuffers();
        EmitTextures();
        EmitStorageBuffers();
        EmitStageStructs();
        EmitStaticMirrors();
        EmitGlobalConsts();
        EmitFunctions();
        EmitWrapperMain();
        return m_out.str();
    }

private:
    const Program& m_program;
    const ShaderIRModule& m_module;
    std::ostringstream m_out;
    bool m_inEntryFunction = false;

    void EmitUniformBuffers()
    {
        for (const IRUniformBufferBinding& ub : m_module.uniformBuffers)
        {
            int set = GroupSetIndex(ub.group);
            m_out << "[[vk::binding(0, " << set << ")]] cbuffer Group" << GroupName(ub.group) << "Constants : register(b0, space" << set << ")\n{\n";
            // Every member placed where the IR says it is, rather than
            // wherever this language would have put it.
            //
            // Not a formality. The engine writes this buffer from the
            // reflected offsets, which give each parameter its own
            // sixteen-byte slot; left to itself, HLSL packs several
            // scalars into one such slot instead. The two layouts then
            // disagree, and what a shader reads as its roughness is
            // whatever the engine wrote three parameters later -- a
            // picture that is wrong in a way nothing reports, because
            // every value involved is a perfectly ordinary number.
            for (const IRUniformBufferMember& m : ub.members)
                m_out << "    " << HLSLTypeName(m.type) << " " << m.name << " : packoffset(c" << (m.offset / 16u) << ");\n";
            m_out << "};\n\n";
        }
    }

    void EmitTextures()
    {
        for (const IRResourceBinding& r : m_module.resources)
        {
            int set = GroupSetIndex(r.group);
            m_out << "[[vk::binding(" << r.binding << ", " << set << ")]] " << HLSLTypeName(r.type) << " " << r.name << " : register(t" << r.binding << ", space" << set << ");\n";

            // A shadow map's sampler is a different object, not a flag on
            // the ordinary one: it carries the comparison the hardware
            // performs before filtering, which is the whole reason to
            // sample this way rather than read and compare afterwards.
            const char* samplerType = r.type.kind == TypeKind::Sampler2DShadow ? "SamplerComparisonState" : "SamplerState";
            m_out << "[[vk::binding(" << r.samplerBinding << ", " << set << ")]] " << samplerType << " " << r.name << "_sampler : register(s" << r.samplerBinding << ", space" << set << ");\n";
        }
        if (!m_module.resources.empty()) m_out << "\n";
    }

    void EmitStorageBuffers()
    {
        // A read-write UAV (RWStructuredBuffer, `u` register space) only
        // in a compute shader -- Vulkan rejects a storage-buffer variable
        // in the fragment stage that isn't decorated NonWritable unless
        // the fragmentStoresAndAtomics feature is explicitly enabled
        // (which this backend's Fluxion_RHI_CreateDevice call never
        // requests), so a vertex/fragment shader that only ever reads a
        // [Buffer(Group)] declares it as the read-only StructuredBuffer
        // (`t` register/SRV) instead -- same underlying SSBO on the RHI
        // side either way, just a different SPIR-V access decoration.
        bool readOnly = m_module.stage != ShaderStage::Compute;
        for (const IRResourceBinding& b : m_module.storageBuffers)
        {
            int set = GroupSetIndex(b.group);
            const char* bufferType = readOnly ? "StructuredBuffer" : "RWStructuredBuffer";
            char registerSpace = readOnly ? 't' : 'u';
            m_out << "[[vk::binding(" << b.binding << ", " << set << ")]] " << bufferType << "<" << HLSLTypeName(b.type) << "> " << b.name
                << " : register(" << registerSpace << b.binding << ", space" << set << ");\n";
        }
        if (!m_module.storageBuffers.empty()) m_out << "\n";
    }

    void EmitComputeStaticMirrors()
    {
        // Same file-scope "static mirror" idiom as EmitStaticMirrors:
        // shaderMain() references `ThreadID` directly, but the actual
        // SV_DispatchThreadID value only exists as a wrapper-local
        // parameter, so it's mirrored into a static global the wrapper
        // assigns before calling shaderMain().
        m_out << "static uint ThreadID;\n\n";
    }

    void EmitComputeWrapperMain()
    {
        m_out << "[numthreads(" << m_module.localSizeX << ", 1, 1)]\n";
        m_out << "void main(uint3 dispatchThreadID : SV_DispatchThreadID)\n{\n";
        m_out << "    ThreadID = dispatchThreadID.x;\n";
        m_out << "    shaderMain();\n";
        m_out << "}\n";
    }

    void EmitStageStructs()
    {
        m_out << "struct StageInput\n{\n";
        for (const IRStageIOField& f : m_module.inputs)
            m_out << "    [[vk::location(" << f.location << ")]] " << HLSLTypeName(f.type) << " " << f.name << " : TEXCOORD" << f.location << ";\n";
        m_out << "};\n\n";

        m_out << "struct StageOutput\n{\n";
        for (const IRStageIOField& f : m_module.outputs)
        {
            if (m_module.stage == ShaderStage::Vertex && f.name == "Position") continue;
            m_out << "    [[vk::location(" << f.location << ")]] " << HLSLTypeName(f.type) << " " << f.name << " : TEXCOORD" << f.location << ";\n";
        }
        if (m_module.stage == ShaderStage::Vertex)
            m_out << "    float4 Position : SV_Position;\n";
        for (size_t i = 0; i < m_module.outputSlots.size(); ++i)
            m_out << "    " << HLSLTypeName(m_module.outputSlots[i].type) << " " << m_module.outputSlots[i].name << " : SV_Target" << m_module.outputSlots[i].slot << ";\n";
        m_out << "};\n\n";
    }

    void EmitStaticMirrors()
    {
        // The instance built-in is a mirror like the rest, but its value
        // comes from a system value on the entry point rather than from
        // the stage-input struct -- see EmitWrapperMain.
        if (m_module.stage == ShaderStage::Vertex) m_out << "static int InstanceIndex;\n";

        for (const IRStageIOField& f : m_module.inputs)
            m_out << "static " << HLSLTypeName(f.type) << " " << f.name << ";\n";
        for (const IRStageIOField& f : m_module.outputs)
            m_out << "static " << HLSLTypeName(f.type) << " " << f.name << ";\n";
        for (const IROutputSlot& o : m_module.outputSlots)
            m_out << "static " << HLSLTypeName(o.type) << " " << o.name << ";\n";
        m_out << "\n";
    }

    // `static` on purpose, and not merely `const`.
    //
    // A global in HLSL without it is not a constant at all: it becomes a
    // member of an implicit constant buffer the shader never fills in, so
    // the value would read as zero at runtime rather than as what the
    // source said. `static const` is a real compile-time constant.
    // Before the consts and the functions, because both may name one.
    void EmitStructs()
    {
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Struct) continue;
            auto* d = static_cast<StructDecl*>(decl.get());
            m_out << "struct " << d->name << "\n{\n";
            for (const StructField& field : d->fields)
                m_out << "    " << HLSLTypeName(field.type) << " " << field.name << ";\n";
            m_out << "};\n\n";
        }
    }

    void EmitGlobalConsts()
    {
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::GlobalConst) continue;
            auto* d = static_cast<GlobalConstDecl*>(decl.get());
            m_out << "static const " << HLSLTypeName(d->type) << " " << d->name << " = ";
            EmitExpr(*d->initializer);
            m_out << ";\n";
        }
    }

    void EmitFunctions()
    {
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Function) continue;
            auto* fn = static_cast<FunctionDecl*>(decl.get());
            bool isEntry = fn->name == m_module.entryPoint;
            m_out << HLSLTypeName(fn->returnType) << " " << (isEntry ? "shaderMain" : fn->name) << "(";
            for (size_t i = 0; i < fn->params.size(); ++i)
            {
                if (i) m_out << ", ";
                m_out << HLSLTypeName(fn->params[i].type) << " " << fn->params[i].name;
            }
            m_out << ")\n";
            m_inEntryFunction = isEntry;
            EmitStmt(*fn->body, 0);
            m_inEntryFunction = false;
            m_out << "\n";
        }
    }

    void EmitWrapperMain()
    {
        bool isVertex = m_module.stage == ShaderStage::Vertex;

        // SV_InstanceID COUNTS FROM ZERO within the draw: it does not
        // include the start instance, which is the opposite of what
        // Vulkan's gl_InstanceIndex does. The engine never sets a start
        // instance, so the two never get the chance to disagree -- and
        // taking the value here without adding anything to it is this
        // side of that promise.
        if (isVertex) m_out << "StageOutput main(StageInput input, uint fluxionInstanceID : SV_InstanceID)\n{\n";
        else m_out << "StageOutput main(StageInput input)\n{\n";
        if (isVertex) m_out << "    InstanceIndex = (int)fluxionInstanceID;\n";

        for (const IRStageIOField& f : m_module.inputs)
            m_out << "    " << f.name << " = input." << f.name << ";\n";
        m_out << "    shaderMain();\n";
        m_out << "    StageOutput output;\n";
        for (const IRStageIOField& f : m_module.outputs)
        {
            if (isVertex && f.name == "Position") { m_out << "    output.Position = Position;\n"; continue; }
            m_out << "    output." << f.name << " = " << f.name << ";\n";
        }
        for (const IROutputSlot& o : m_module.outputSlots)
            m_out << "    output." << o.name << " = " << o.name << ";\n";
        m_out << "    return output;\n}\n";
    }

    void Indent(int depth) { for (int i = 0; i < depth; ++i) m_out << "    "; }

    void EmitStmt(const Stmt& stmt, int depth)
    {
        switch (stmt.kind)
        {
            case StmtKind::Block: {
                Indent(depth); m_out << "{\n";
                for (const StmtPtr& s : static_cast<const BlockStmt&>(stmt).statements) EmitStmt(*s, depth + 1);
                Indent(depth); m_out << "}\n";
                break;
            }
            case StmtKind::Expr: {
                Indent(depth); EmitExpr(*static_cast<const ExprStmt&>(stmt).expr); m_out << ";\n";
                break;
            }
            case StmtKind::VarDecl: {
                auto& s = static_cast<const VarDeclStmt&>(stmt);
                Indent(depth); m_out << HLSLTypeName(s.type) << " " << s.name;
                if (s.initializer) { m_out << " = "; EmitExpr(*s.initializer); }
                m_out << ";\n";
                break;
            }
            case StmtKind::If: {
                auto& s = static_cast<const IfStmt&>(stmt);
                Indent(depth); m_out << "if ("; EmitExpr(*s.condition); m_out << ")\n";
                EmitStmt(*s.thenBranch, depth);
                if (s.elseBranch) { Indent(depth); m_out << "else\n"; EmitStmt(*s.elseBranch, depth); }
                break;
            }
            case StmtKind::For: {
                auto& s = static_cast<const ForStmt&>(stmt);
                Indent(depth); m_out << "for (";
                if (s.init && s.init->kind == StmtKind::VarDecl)
                {
                    auto& v = static_cast<const VarDeclStmt&>(*s.init);
                    m_out << HLSLTypeName(v.type) << " " << v.name;
                    if (v.initializer) { m_out << " = "; EmitExpr(*v.initializer); }
                }
                m_out << "; "; if (s.condition) EmitExpr(*s.condition); m_out << "; ";
                if (s.iteration) EmitExpr(*s.iteration);
                m_out << ")\n";
                EmitStmt(*s.body, depth);
                break;
            }
            case StmtKind::While: {
                auto& s = static_cast<const WhileStmt&>(stmt);
                Indent(depth); m_out << "while ("; EmitExpr(*s.condition); m_out << ")\n";
                EmitStmt(*s.body, depth);
                break;
            }
            case StmtKind::Return: {
                auto& s = static_cast<const ReturnStmt&>(stmt);
                // Inside the entry function, `return expr;` routes the
                // value to the function's designated [Target(N)]/
                // [Output] rather than actually returning it from what
                // HLSL sees as a void wrapper body.
                if (m_inEntryFunction && s.value && m_module.returnTarget.has_value())
                {
                    Indent(depth);
                    m_out << *m_module.returnTarget << " = ";
                    EmitExpr(*s.value);
                    m_out << "; return;\n";
                    break;
                }
                Indent(depth); m_out << "return";
                if (s.value) { m_out << " "; EmitExpr(*s.value); }
                m_out << ";\n";
                break;
            }
            case StmtKind::Discard: {
                // Spelled the same in both target languages, which is
                // luck rather than design -- and the reason it is written
                // out at each backend rather than shared.
                Indent(depth); m_out << "discard;\n";
                break;
            }
        }
    }

    void EmitExpr(const Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral: m_out << static_cast<const IntLiteralExpr&>(expr).value; break;
            case ExprKind::FloatLiteral: m_out << static_cast<const FloatLiteralExpr&>(expr).value; break;
            case ExprKind::BoolLiteral: m_out << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false"); break;
            case ExprKind::VarRef: m_out << static_cast<const VarRefExpr&>(expr).name; break;
            case ExprKind::Unary: {
                auto& e = static_cast<const UnaryExpr&>(expr);
                m_out << (e.op == UnaryOp::Not ? "!" : "-") << "("; EmitExpr(*e.operand); m_out << ")";
                break;
            }
            case ExprKind::Binary: {
                auto& e = static_cast<const BinaryExpr&>(expr);
                // HLSL's `*` between a matrix and a vector (or another
                // matrix) is component-wise, not a real matrix multiply --
                // unlike this language's own semantics (and GLSL's `*`,
                // which the GLSL backend can emit unchanged), so a real
                // `mvp * Vector4(...)` in .jsl source needs HLSL's
                // dedicated `mul(matrix, vector)` intrinsic here instead of
                // a literal `*` token, or dxc silently produces a
                // dimension-mismatch compile error (or worse, wrong,
                // silently-broadcast results if the shapes happened to
                // align).
                bool isLhsMatrix = e.lhs->resolvedType.kind == TypeKind::Mat3 || e.lhs->resolvedType.kind == TypeKind::Mat4;
                bool isRhsMatrixOrVector = e.rhs->resolvedType.kind == TypeKind::Mat3 || e.rhs->resolvedType.kind == TypeKind::Mat4 ||
                    e.rhs->resolvedType.kind == TypeKind::Vec3 || e.rhs->resolvedType.kind == TypeKind::Vec4;
                if (e.op == BinaryOp::Mul && isLhsMatrix && isRhsMatrixOrVector)
                {
                    m_out << "mul("; EmitExpr(*e.lhs); m_out << ", "; EmitExpr(*e.rhs); m_out << ")";
                    break;
                }
                m_out << "("; EmitExpr(*e.lhs); m_out << " " << BinaryOpText(e.op) << " "; EmitExpr(*e.rhs); m_out << ")";
                break;
            }
            case ExprKind::Member: {
                auto& e = static_cast<const MemberExpr&>(expr);
                EmitExpr(*e.base); m_out << "." << e.member;
                break;
            }
            case ExprKind::Index: {
                auto& e = static_cast<const IndexExpr&>(expr);
                EmitExpr(*e.base); m_out << "["; EmitExpr(*e.index); m_out << "]";
                break;
            }
            case ExprKind::Constructor: {
                auto& e = static_cast<const ConstructorExpr&>(expr);
                m_out << HLSLTypeName(e.targetType) << "(";
                for (size_t i = 0; i < e.args.size(); ++i) { if (i) m_out << ", "; EmitExpr(*e.args[i]); }
                m_out << ")";
                break;
            }
            case ExprKind::Ternary: {
                auto& e = static_cast<const TernaryExpr&>(expr);
                m_out << "("; EmitExpr(*e.condition); m_out << " ? "; EmitExpr(*e.thenExpr); m_out << " : "; EmitExpr(*e.elseExpr); m_out << ")";
                break;
            }
            case ExprKind::Assign: {
                auto& e = static_cast<const AssignExpr&>(expr);
                EmitExpr(*e.target); m_out << " " << AssignOpText(e.op) << " "; EmitExpr(*e.value);
                break;
            }
            case ExprKind::Call: {
                auto& e = static_cast<const CallExpr&>(expr);
                if (IsTextureSampleCall(e.callee) && e.args.size() == 2 && e.args[0]->kind == ExprKind::VarRef)
                {
                    const std::string& texName = static_cast<const VarRefExpr&>(*e.args[0]).name;
                    m_out << texName << ".Sample(" << texName << "_sampler, "; EmitExpr(*e.args[1]); m_out << ")";
                    break;
                }
                if (IsTextureCompareCall(e.callee) && e.args.size() == 3 && e.args[0]->kind == ExprKind::VarRef)
                {
                    const std::string& texName = static_cast<const VarRefExpr&>(*e.args[0]).name;
                    m_out << texName << ".SampleCmpLevelZero(" << texName << "_sampler, ";
                    EmitExpr(*e.args[1]);
                    m_out << ", ";
                    EmitExpr(*e.args[2]);
                    m_out << ")";
                    break;
                }
                if (IsTextureSampleLevelCall(e.callee) && e.args.size() == 3 && e.args[0]->kind == ExprKind::VarRef)
                {
                    const std::string& texName = static_cast<const VarRefExpr&>(*e.args[0]).name;
                    m_out << texName << ".SampleLevel(" << texName << "_sampler, ";
                    EmitExpr(*e.args[1]);
                    m_out << ", ";
                    EmitExpr(*e.args[2]);
                    m_out << ")";
                    break;
                }
                m_out << RemapCallName(e.callee) << "(";
                for (size_t i = 0; i < e.args.size(); ++i) { if (i) m_out << ", "; EmitExpr(*e.args[i]); }
                m_out << ")";
                break;
            }
        }
    }
};

} // namespace

std::string EmitHLSL(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics)
{
    // Taken and not used, for the same reason EmitGLSL does not use it:
    // everything this walks over was decided to be valid before emission
    // began, and the parameter stays so that every backend is asked the
    // same way.
    (void)diagnostics;

    HLSLEmitter emitter(program, module);
    return emitter.Run();
}

} // namespace Fluxion::ShaderCompiler
