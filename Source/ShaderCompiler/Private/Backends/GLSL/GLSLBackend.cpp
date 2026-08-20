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

#include <Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp>

#include <sstream>

namespace Fluxion::ShaderCompiler
{

// A FLOAT LITERAL THAT STILL LOOKS LIKE ONE.
//
// Streaming a float prints 1.0 as "1" -- and "1" in both of the
// languages this compiles to is an INTEGER. Most of the time nothing
// notices, because one side of the expression is a float and the whole
// thing is promoted; the exception is two of them meeting, and the
// commonest way for that to happen is a constant weight written as a
// division.
//
// (1.0 / 16.0) came out as (1 / 16), which is integer division, which is
// ZERO. Not an error, not a warning: a shader that multiplied by it
// returned black, and the pass built on it produced nothing at all.
// Measured, in the bloom chain, where the glow simply never appeared.
static std::string FluxionShaderCompilerInternal_FloatLiteral(double value)
{
    std::ostringstream text;
    text.precision(9);
    text << value;

    std::string printed = text.str();

    // An exponent or a point already says what it is; so does a name like
    // "inf", which no decimal point would help.
    if (printed.find('.') == std::string::npos && printed.find('e') == std::string::npos &&
        printed.find('E') == std::string::npos && printed.find_first_of("in") == std::string::npos)
    {
        printed += ".0";
    }
    return printed;
}

namespace
{

std::string GLSLTypeName(const ShaderType& type)
{
    switch (type.kind)
    {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Uint: return "uint";
        case TypeKind::Float: return "float";
        case TypeKind::Vec2: return "vec2";
        case TypeKind::Vec3: return "vec3";
        case TypeKind::Vec4: return "vec4";
        case TypeKind::Mat3: return "mat3";
        case TypeKind::Mat4: return "mat4";
        case TypeKind::Sampler2D: return "sampler2D";
        case TypeKind::SamplerCube: return "samplerCube";

        // Combined image and sampler, like the two above -- the
        // comparison itself is state on the sampler object the RHI
        // binds, not part of this name.
        case TypeKind::Sampler2DShadow: return "sampler2DShadow";

        // Same reasoning as the other backend: a declared struct is
        // emitted under its own name, because silently becoming a float
        // compiles and is wrong.
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
    if (name == "texture2D" || name == "tex2D") return "texture";
    return name;
}

// Same convention as the HLSL backend: a BindingGroup's enum value is
// its descriptor set index directly (matches FLUXION_RHI_BIND_GROUP_*).
int GroupSetIndex(BindingGroup group) { return (int)group; }

// Unlike the HLSL backend's `[[vk::binding(B, S)]]` (consumed only by
// dxc/SPIR-V, which understands descriptor sets), this GLSL text is fed
// directly to a real OpenGL driver's GLSL compiler by the RHI's OpenGL
// backend -- and core-profile GLSL has no `set` concept at all
// (`layout(set = ...)` is a glslang/Vulkan-GLSL extension a stock driver
// rejects outright). The OpenGL backend instead partitions one flat GL
// binding-point namespace per BindingGroup with a fixed stride
// (`Fluxion_RHIOpenGL_CommandListSetBindGroup` in
// Source/RHI/Private/OpenGL/OpenGLBinding.cpp: `groupIndex *
// FLUXION_RHIOPENGL_BINDINGS_PER_GROUP + entry.binding`) -- this must
// stay numerically identical to that RHI-side constant, or a shader's
// declared binding and the buffer/texture the RHI backend actually binds
// at that GL binding point silently disagree.
constexpr int kOpenGLBindingsPerGroup = 16;
int OpenGLFlatBinding(BindingGroup group, int localBinding) { return GroupSetIndex(group) * kOpenGLBindingsPerGroup + localBinding; }

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

class GLSLEmitter
{
public:
    GLSLEmitter(const Program& program, const ShaderIRModule& module, const GLSLOptions& options)
        : m_program(program), m_module(module), m_options(options)
    {
    }

    std::string Run()
    {
        m_out << "#version " << m_options.versionDirective << "\n\n";
        if (m_module.stage == ShaderStage::Compute)
            m_out << "layout(local_size_x = " << m_module.localSizeX << ", local_size_y = 1, local_size_z = 1) in;\n\n";
        // Structs first, before anything that could be made OF one.
        //
        // A uniform block or a storage buffer may have a user struct as
        // its element type, and a struct used before it is declared is a
        // compile error in the generated text -- reported against a line
        // the shader's author never wrote. This ordering is the only
        // thing preventing that, so it is not free to rearrange.
        EmitStageIO();
        EmitOutputSlots();
        EmitStructs();
        EmitTextures();
        EmitUniformBuffers();
        EmitStorageBuffers();
        m_out << "\n";
        EmitGlobalConsts();
        EmitFunctions();
        return m_out.str();
    }

private:
    const Program& m_program;
    const ShaderIRModule& m_module;
    GLSLOptions m_options;
    std::ostringstream m_out;
    bool m_inEntryFunction = false;

    void EmitStageIO()
    {
        for (const IRStageIOField& f : m_module.inputs)
            m_out << "layout(location = " << f.location << ") in " << GLSLTypeName(f.type) << " " << f.name << ";\n";
        for (const IRStageIOField& f : m_module.outputs)
        {
            // GLSL already provides the vertex clip-space output as the
            // built-in `gl_Position` -- a user `out vec4 Position;`
            // declaration would just be a second, unrelated variable, so
            // this one field is skipped here and its references are
            // redirected to `gl_Position` in EmitExpr instead.
            if (m_module.stage == ShaderStage::Vertex && f.name == "Position") continue;
            m_out << "layout(location = " << f.location << ") out " << GLSLTypeName(f.type) << " " << f.name << ";\n";
        }
    }

    void EmitOutputSlots()
    {
        for (const IROutputSlot& o : m_module.outputSlots)
            m_out << "layout(location = " << o.slot << ") out " << GLSLTypeName(o.type) << " " << o.name << ";\n";
    }

    void EmitTextures()
    {
        // GLSL's sampler2D/samplerCube is already a single combined
        // image+sampler descriptor -- unlike the HLSL backend, there is
        // no separate sampler binding to emit here, so only r.binding
        // (not r.samplerBinding) is meaningful for this backend.
        for (const IRResourceBinding& r : m_module.resources)
            m_out << "layout(binding = " << OpenGLFlatBinding(r.group, r.binding) << ") uniform " << GLSLTypeName(r.type) << " " << r.name << ";\n";
    }

    void EmitUniformBuffers()
    {
        for (const IRUniformBufferBinding& ub : m_module.uniformBuffers)
        {
            m_out << "layout(std140, binding = " << OpenGLFlatBinding(ub.group, 0) << ") uniform Group" << GroupName(ub.group) << "Block\n{\n";
            // Placed explicitly, for the reason set out in the HLSL
            // backend's own version of this loop: the engine writes this
            // buffer from the reflected offsets, and a language left to
            // pack it its own way arrives at a different arrangement.
            for (const IRUniformBufferMember& m : ub.members)
                m_out << "    layout(offset = " << m.offset << ") " << GLSLTypeName(m.type) << " " << m.name << ";\n";
            m_out << "};\n";
        }
    }

    void EmitStorageBuffers()
    {
        // Same std430-block style as EmitUniformBuffers, named
        // ...StorageBlock instead of ...Block to distinguish a
        // read-write [Buffer(Group)] from a read-only [Uniform(Group)].
        for (const IRResourceBinding& b : m_module.storageBuffers)
        {
            // The buffer's own name is part of the block's, not only the
            // group's. A block is named once per BUFFER, and two buffers
            // in one group -- the frame's lights and its irradiance --
            // would otherwise both declare a block of the same name,
            // which this language rejects as a redeclaration.
            m_out << "layout(std430, binding = " << OpenGLFlatBinding(b.group, b.binding) << ") buffer Group" << GroupName(b.group) << "_" << b.name << "_StorageBlock\n{\n";
            m_out << "    " << GLSLTypeName(b.type) << " " << b.name << "[];\n";
            m_out << "};\n";
        }
    }

    void EmitStructs()
    {
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Struct) continue;
            auto* d = static_cast<StructDecl*>(decl.get());
            m_out << "struct " << d->name << "\n{\n";
            for (const StructField& field : d->fields)
                m_out << "    " << GLSLTypeName(field.type) << " " << field.name << ";\n";
            m_out << "};\n\n";
        }
    }

    void EmitGlobalConsts()
    {
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::GlobalConst) continue;
            auto* d = static_cast<GlobalConstDecl*>(decl.get());
            m_out << "const " << GLSLTypeName(d->type) << " " << d->name << " = ";
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
            m_out << GLSLTypeName(fn->returnType) << " " << fn->name << "(";
            for (size_t i = 0; i < fn->params.size(); ++i)
            {
                if (i) m_out << ", ";
                m_out << GLSLTypeName(fn->params[i].type) << " " << fn->params[i].name;
            }
            m_out << ")\n";
            m_inEntryFunction = (fn->name == m_module.entryPoint);
            EmitStmt(*fn->body, 0);
            m_inEntryFunction = false;
            m_out << "\n";
        }
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
                Indent(depth); m_out << GLSLTypeName(s.type) << " " << s.name;
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
                    m_out << GLSLTypeName(v.type) << " " << v.name;
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
                // Same routing as the HLSL backend: inside the entry
                // function, `return expr;` assigns to the designated
                // output/target, then returns (this language's `main()`
                // is void, same as the RHI's own current pipeline
                // contract).
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
            case ExprKind::FloatLiteral:
                m_out << FluxionShaderCompilerInternal_FloatLiteral(static_cast<const FloatLiteralExpr&>(expr).value);
                break;
            case ExprKind::BoolLiteral: m_out << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false"); break;
            case ExprKind::VarRef: {
                const std::string& name = static_cast<const VarRefExpr&>(expr).name;
                bool isVertexPosition = m_module.stage == ShaderStage::Vertex && name == "Position";
                bool isThreadID = m_module.stage == ShaderStage::Compute && name == "ThreadID";

                // gl_InstanceID, not gl_InstanceIndex: this backend emits
                // core-profile GLSL for OpenGL, where the built-in counts
                // from zero within the draw. (The Vulkan-flavoured
                // gl_InstanceIndex, which adds the base instance, is a
                // different number -- and the engine never sets a base
                // instance precisely so the two cannot disagree.)
                bool isInstanceIndex = m_module.stage == ShaderStage::Vertex && name == "InstanceIndex";

                if (isVertexPosition) m_out << "gl_Position";
                else if (isThreadID) m_out << "gl_GlobalInvocationID.x";
                else if (isInstanceIndex) m_out << "gl_InstanceID";
                else m_out << name;
                break;
            }
            case ExprKind::Unary: {
                auto& e = static_cast<const UnaryExpr&>(expr);
                m_out << (e.op == UnaryOp::Not ? "!" : "-") << "("; EmitExpr(*e.operand); m_out << ")";
                break;
            }
            case ExprKind::Binary: {
                auto& e = static_cast<const BinaryExpr&>(expr);
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
                m_out << GLSLTypeName(e.targetType) << "(";
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

                // An ordinary expression in this language: it answers
                // with the value it replaced. The other one needs a
                // statement and an out parameter, which is why the
                // language only allows this where a variable is being
                // declared -- see the HLSL backend and BuildIR.
                if (e.callee == "AtomicAdd" && e.args.size() == 2)
                {
                    m_out << "atomicAdd("; EmitExpr(*e.args[0]); m_out << ", "; EmitExpr(*e.args[1]); m_out << ")";
                    break;
                }

                // The comparison sample has no name of its own here: this
                // language spells it as an ordinary sample whose
                // coordinate carries the depth to compare against in its
                // last component. Three arguments become two.
                if (e.callee == "textureCompare" && e.args.size() == 3)
                {
                    m_out << "texture(";
                    EmitExpr(*e.args[0]);
                    m_out << ", vec3(";
                    EmitExpr(*e.args[1]);
                    m_out << ", ";
                    EmitExpr(*e.args[2]);
                    m_out << "))";
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

std::string EmitGLSL(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics, const GLSLOptions& options)
{
    // Taken and not used. Emission happens after the analysis that
    // decides whether the program is valid at all, so by this point there
    // is nothing left to refuse -- every text this walks over has already
    // been checked. The parameter stays because a backend is entitled to
    // report something the others cannot (a target that cannot express a
    // construct the language allows), and a signature that differed
    // between backends would make the caller pick which one it was
    // talking to.
    (void)diagnostics;

    GLSLEmitter emitter(program, module, options);
    return emitter.Run();
}

} // namespace Fluxion::ShaderCompiler
