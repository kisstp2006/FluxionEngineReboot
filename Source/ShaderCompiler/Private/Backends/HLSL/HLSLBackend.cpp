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
            EmitUniformBuffers();
            EmitTextures();
            EmitStorageBuffers();
            EmitComputeStaticMirrors();
            EmitStructs();
            EmitGlobalConsts();
            EmitFunctions();
            EmitComputeWrapperMain();
            return m_out.str();
        }

        EmitUniformBuffers();
        EmitTextures();
        EmitStorageBuffers();
        EmitStageStructs();
        EmitStaticMirrors();
        EmitStructs();
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
            for (const IRUniformBufferMember& m : ub.members)
                m_out << "    " << HLSLTypeName(m.type) << " " << m.name << ";\n";
            m_out << "};\n\n";
        }
    }

    void EmitTextures()
    {
        for (const IRResourceBinding& r : m_module.resources)
        {
            int set = GroupSetIndex(r.group);
            m_out << "[[vk::binding(" << r.binding << ", " << set << ")]] " << HLSLTypeName(r.type) << " " << r.name << " : register(t" << r.binding << ", space" << set << ");\n";
            m_out << "[[vk::binding(" << r.samplerBinding << ", " << set << ")]] SamplerState " << r.name << "_sampler : register(s" << r.samplerBinding << ", space" << set << ");\n";
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
        m_out << "StageOutput main(StageInput input)\n{\n";
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
