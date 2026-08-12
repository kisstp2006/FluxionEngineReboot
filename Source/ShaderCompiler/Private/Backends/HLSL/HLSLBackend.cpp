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

class HLSLEmitter
{
public:
    HLSLEmitter(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics)
        : m_program(program), m_module(module), m_diagnostics(diagnostics)
    {
    }

    std::string Run()
    {
        EmitPushConstants();
        EmitTextures();
        EmitStageStructs();
        EmitStaticMirrors();
        EmitFunctions();
        EmitWrapperMain();
        return m_out.str();
    }

private:
    const Program& m_program;
    const ShaderIRModule& m_module;
    DiagnosticList& m_diagnostics;
    std::ostringstream m_out;
    bool m_inEntryFunction = false;

    void EmitPushConstants()
    {
        if (m_module.pushConstants.empty()) return;
        m_out << "struct PushConstants\n{\n";
        for (const IRPushConstantMember& m : m_module.pushConstants)
            m_out << "    " << HLSLTypeName(m.type) << " " << m.name << ";\n";
        m_out << "};\n[[vk::push_constant]] PushConstants pc;\n\n";
    }

    void EmitTextures()
    {
        for (const IRResourceBinding& r : m_module.resources)
        {
            m_out << HLSLTypeName(r.type) << " " << r.name << " : register(t" << r.slot << ");\n";
            m_out << "SamplerState " << r.name << "_sampler : register(s" << r.slot << ");\n";
        }
        if (!m_module.resources.empty()) m_out << "\n";
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
    HLSLEmitter emitter(program, module, diagnostics);
    return emitter.Run();
}

} // namespace Fluxion::ShaderCompiler
