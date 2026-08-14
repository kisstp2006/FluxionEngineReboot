#include <Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp>

#include <sstream>

namespace Fluxion::ShaderCompiler
{

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
        EmitStageIO();
        EmitOutputSlots();
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
            m_out << "layout(binding = " << OpenGLFlatBinding(ub.group, 0) << ") uniform Group" << GroupName(ub.group) << "Block\n{\n";
            for (const IRUniformBufferMember& m : ub.members)
                m_out << "    " << GLSLTypeName(m.type) << " " << m.name << ";\n";
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
            m_out << "layout(std430, binding = " << OpenGLFlatBinding(b.group, b.binding) << ") buffer Group" << GroupName(b.group) << "StorageBlock\n{\n";
            m_out << "    " << GLSLTypeName(b.type) << " " << b.name << "[];\n";
            m_out << "};\n";
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
        }
    }

    void EmitExpr(const Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral: m_out << static_cast<const IntLiteralExpr&>(expr).value; break;
            case ExprKind::FloatLiteral: m_out << static_cast<const FloatLiteralExpr&>(expr).value; break;
            case ExprKind::BoolLiteral: m_out << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false"); break;
            case ExprKind::VarRef: {
                const std::string& name = static_cast<const VarRefExpr&>(expr).name;
                bool isVertexPosition = m_module.stage == ShaderStage::Vertex && name == "Position";
                bool isThreadID = m_module.stage == ShaderStage::Compute && name == "ThreadID";
                if (isVertexPosition) m_out << "gl_Position";
                else if (isThreadID) m_out << "gl_GlobalInvocationID.x";
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
