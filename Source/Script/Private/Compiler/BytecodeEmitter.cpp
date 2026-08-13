#include <Fluxion/Script/Compiler/BytecodeEmitter.hpp>

#include <bit>
#include <unordered_map>
#include <utility>

namespace Fluxion::Script
{

namespace
{

OpCode ArithmeticOpcode(BinaryOp op, ValueType operandType)
{
    const bool isFloat = operandType == ValueType::Float;
    switch (op)
    {
        case BinaryOp::Add:
            if (operandType == ValueType::String) return OpCode::ConcatString;
            return isFloat ? OpCode::AddFloat : OpCode::AddInt;
        case BinaryOp::Sub: return isFloat ? OpCode::SubFloat : OpCode::SubInt;
        case BinaryOp::Mul: return isFloat ? OpCode::MulFloat : OpCode::MulInt;
        case BinaryOp::Div: return isFloat ? OpCode::DivFloat : OpCode::DivInt;
        case BinaryOp::Mod: return isFloat ? OpCode::ModFloat : OpCode::ModInt;
        default: return OpCode::Nop;
    }
}

OpCode ComparisonOpcode(BinaryOp op, ValueType operandType)
{
    switch (operandType)
    {
        case ValueType::Float:
            switch (op)
            {
                case BinaryOp::Equal: return OpCode::EqualFloat;
                case BinaryOp::NotEqual: return OpCode::NotEqualFloat;
                case BinaryOp::Less: return OpCode::LessFloat;
                case BinaryOp::Greater: return OpCode::GreaterFloat;
                case BinaryOp::LessEqual: return OpCode::LessEqualFloat;
                case BinaryOp::GreaterEqual: return OpCode::GreaterEqualFloat;
                default: return OpCode::Nop;
            }
        case ValueType::Bool:
            return op == BinaryOp::Equal ? OpCode::EqualBool : OpCode::NotEqualBool;
        case ValueType::String:
            return op == BinaryOp::Equal ? OpCode::EqualString : OpCode::NotEqualString;
        default:
            switch (op)
            {
                case BinaryOp::Equal: return OpCode::EqualInt;
                case BinaryOp::NotEqual: return OpCode::NotEqualInt;
                case BinaryOp::Less: return OpCode::LessInt;
                case BinaryOp::Greater: return OpCode::GreaterInt;
                case BinaryOp::LessEqual: return OpCode::LessEqualInt;
                case BinaryOp::GreaterEqual: return OpCode::GreaterEqualInt;
                default: return OpCode::Nop;
            }
    }
}

BinaryOp BinaryOpForCompound(AssignOp op)
{
    switch (op)
    {
        case AssignOp::AddAssign: return BinaryOp::Add;
        case AssignOp::SubAssign: return BinaryOp::Sub;
        case AssignOp::MulAssign: return BinaryOp::Mul;
        case AssignOp::DivAssign: return BinaryOp::Div;
        default: return BinaryOp::Add;
    }
}

class EmitterState
{
public:
    EmitterState(const Program& program, std::string sourceName, u32 moduleVersion, DiagnosticList& diagnostics)
        : m_program(program), m_sourceName(std::move(sourceName)), m_moduleVersion(moduleVersion), m_diagnostics(diagnostics)
    {
    }

    BytecodeModule Run()
    {
        m_module.sourceName = m_sourceName;
        m_module.header.moduleVersion = m_moduleVersion;

        std::vector<MethodDecl*> methods;
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Class) continue;
            auto* classDecl = static_cast<ClassDecl*>(decl.get());
            for (const DeclPtr& methodDecl : classDecl->methods)
                methods.push_back(static_cast<MethodDecl*>(methodDecl.get()));
        }

        m_module.functions.resize(methods.size());
        for (MethodDecl* method : methods)
        {
            if (method->functionIndex >= m_module.functions.size())
            {
                m_diagnostics.AddError(method->location, "method '" + method->qualifiedName + "' was not assigned a valid slot in the module");
                continue;
            }
            EmitMethod(*method, m_module.functions[method->functionIndex]);
        }

        return std::move(m_module);
    }

private:
    const Program& m_program;
    std::string m_sourceName;
    u32 m_moduleVersion = 0;
    DiagnosticList& m_diagnostics;

    BytecodeModule m_module;

    // Instructions of the function currently being emitted; appended to
    // the module's flat stream once complete, which is what makes jump
    // targets function-relative.
    std::vector<Instruction> m_code;

    struct LoopContext
    {
        std::vector<u32> breakSites;
        std::vector<u32> continueSites;
    };
    std::vector<LoopContext> m_loops;

    std::unordered_map<i32, u32> m_intConstantIndices;
    std::unordered_map<u32, u32> m_floatConstantIndices; // keyed by bit pattern, so -0 and 0 stay distinct
    std::unordered_map<std::string, u32> m_stringConstantIndices;

    void EmitMethod(const MethodDecl& method, FunctionInfo& outInfo)
    {
        m_code.clear();
        m_loops.clear();

        if (method.body) EmitStmt(*method.body);

        // Every function ends with an explicit return instruction, even
        // when the body already returned on every path. A construct that
        // sits last in a body patches its exit jump to "one past the last
        // instruction emitted so far", and this trailing return is what
        // guarantees that landing site exists.
        EmitDefaultReturn(method.returnType);

        outInfo.qualifiedName = method.qualifiedName;
        outInfo.returnType = method.returnType;
        outInfo.parameterTypes.clear();
        for (const ParamDecl& param : method.params) outInfo.parameterTypes.push_back(param.type);

        // A frame is at least large enough for the parameters, whatever
        // the body turned out to need.
        outInfo.localSlotCount = method.localSlotCount;
        if (outInfo.localSlotCount < (u32)method.params.size())
            outInfo.localSlotCount = (u32)method.params.size();

        outInfo.codeOffset = (u32)m_module.code.size();
        outInfo.codeLength = (u32)m_code.size();
        m_module.code.insert(m_module.code.end(), m_code.begin(), m_code.end());
    }

    void EmitDefaultReturn(ValueType returnType)
    {
        switch (returnType)
        {
            case ValueType::Int: Emit(OpCode::PushInt, IntConstant(0)); Emit(OpCode::Return); break;
            case ValueType::Float: Emit(OpCode::PushFloat, FloatConstant(0.0f)); Emit(OpCode::Return); break;
            case ValueType::Bool: Emit(OpCode::PushBool, 0); Emit(OpCode::Return); break;
            case ValueType::String: Emit(OpCode::PushString, StringConstant(std::string())); Emit(OpCode::Return); break;
            default: Emit(OpCode::ReturnVoid); break;
        }
    }

    // --- Emission helpers ---------------------------------------------------

    u32 Here() const { return (u32)m_code.size(); }

    void Emit(OpCode op, u32 operand = 0) { m_code.push_back(Instruction{ op, operand }); }

    u32 EmitJump(OpCode op)
    {
        m_code.push_back(Instruction{ op, 0 });
        return (u32)m_code.size() - 1;
    }

    void PatchJump(u32 site, u32 target) { m_code[site].operand = target; }

    u32 IntConstant(i32 value)
    {
        auto found = m_intConstantIndices.find(value);
        if (found != m_intConstantIndices.end()) return found->second;
        u32 index = (u32)m_module.intConstants.size();
        m_module.intConstants.push_back(value);
        m_intConstantIndices.emplace(value, index);
        return index;
    }

    u32 FloatConstant(f32 value)
    {
        const u32 bits = std::bit_cast<u32>(value);
        auto found = m_floatConstantIndices.find(bits);
        if (found != m_floatConstantIndices.end()) return found->second;
        u32 index = (u32)m_module.floatConstants.size();
        m_module.floatConstants.push_back(value);
        m_floatConstantIndices.emplace(bits, index);
        return index;
    }

    u32 StringConstant(const std::string& value)
    {
        auto found = m_stringConstantIndices.find(value);
        if (found != m_stringConstantIndices.end()) return found->second;
        u32 index = (u32)m_module.stringConstants.size();
        m_module.stringConstants.push_back(value);
        m_stringConstantIndices.emplace(value, index);
        return index;
    }

    u32 SlotOf(const Expr& expr, const char* what)
    {
        if (expr.kind == ExprKind::Identifier)
        {
            const auto& identifier = static_cast<const IdentifierExpr&>(expr);
            if (identifier.localSlot >= 0) return (u32)identifier.localSlot;
        }
        m_diagnostics.AddError(expr.location, std::string("could not determine the storage slot for ") + what);
        return 0;
    }

    // --- Statements ---------------------------------------------------------

    void EmitStmt(const Stmt& stmt)
    {
        switch (stmt.kind)
        {
            case StmtKind::LocalDecl: {
                const auto& node = static_cast<const LocalDeclStmt&>(stmt);
                if (!node.initializer) break; // frame slots start zeroed
                EmitExpr(*node.initializer);
                Emit(OpCode::StoreLocal, node.localSlot >= 0 ? (u32)node.localSlot : 0);
                break;
            }

            case StmtKind::Expr: {
                const auto& node = static_cast<const ExprStmt&>(stmt);
                EmitExpr(*node.expr);
                if (node.expr->resolvedType != ValueType::Void) Emit(OpCode::Pop);
                break;
            }

            case StmtKind::Block: {
                const auto& node = static_cast<const BlockStmt&>(stmt);
                for (const StmtPtr& child : node.statements) EmitStmt(*child);
                break;
            }

            case StmtKind::If: EmitIf(static_cast<const IfStmt&>(stmt)); break;
            case StmtKind::While: EmitWhile(static_cast<const WhileStmt&>(stmt)); break;
            case StmtKind::For: EmitFor(static_cast<const ForStmt&>(stmt)); break;

            case StmtKind::Return: {
                const auto& node = static_cast<const ReturnStmt&>(stmt);
                if (node.value)
                {
                    EmitExpr(*node.value);
                    Emit(OpCode::Return);
                }
                else
                {
                    Emit(OpCode::ReturnVoid);
                }
                break;
            }

            case StmtKind::Break:
                if (!m_loops.empty()) m_loops.back().breakSites.push_back(EmitJump(OpCode::Jump));
                break;

            case StmtKind::Continue:
                if (!m_loops.empty()) m_loops.back().continueSites.push_back(EmitJump(OpCode::Jump));
                break;

            default: break;
        }
    }

    void EmitIf(const IfStmt& stmt)
    {
        EmitExpr(*stmt.condition);
        const u32 skipThen = EmitJump(OpCode::JumpIfFalse);
        EmitStmt(*stmt.thenBranch);

        if (!stmt.elseBranch)
        {
            PatchJump(skipThen, Here());
            return;
        }

        const u32 skipElse = EmitJump(OpCode::Jump);
        PatchJump(skipThen, Here());
        EmitStmt(*stmt.elseBranch);
        PatchJump(skipElse, Here());
    }

    void EmitWhile(const WhileStmt& stmt)
    {
        const u32 conditionLabel = Here();
        EmitExpr(*stmt.condition);
        const u32 exitJump = EmitJump(OpCode::JumpIfFalse);

        m_loops.emplace_back();
        EmitStmt(*stmt.body);
        Emit(OpCode::Jump, conditionLabel);

        const u32 endLabel = Here();
        PatchJump(exitJump, endLabel);
        CloseLoop(conditionLabel, endLabel);
    }

    void EmitFor(const ForStmt& stmt)
    {
        if (stmt.init) EmitStmt(*stmt.init);

        const u32 conditionLabel = Here();
        u32 exitJump = 0;
        bool hasExitJump = false;
        if (stmt.condition)
        {
            EmitExpr(*stmt.condition);
            exitJump = EmitJump(OpCode::JumpIfFalse);
            hasExitJump = true;
        }

        m_loops.emplace_back();
        EmitStmt(*stmt.body);

        // `continue` skips the rest of the body but still runs the step.
        const u32 stepLabel = Here();
        if (stmt.step)
        {
            EmitExpr(*stmt.step);
            if (stmt.step->resolvedType != ValueType::Void) Emit(OpCode::Pop);
        }
        Emit(OpCode::Jump, conditionLabel);

        const u32 endLabel = Here();
        if (hasExitJump) PatchJump(exitJump, endLabel);
        CloseLoop(stepLabel, endLabel);
    }

    void CloseLoop(u32 continueTarget, u32 breakTarget)
    {
        LoopContext context = std::move(m_loops.back());
        m_loops.pop_back();
        for (u32 site : context.breakSites) PatchJump(site, breakTarget);
        for (u32 site : context.continueSites) PatchJump(site, continueTarget);
    }

    // --- Expressions ---------------------------------------------------------

    void EmitExpr(const Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral:
                Emit(OpCode::PushInt, IntConstant((i32)static_cast<const IntLiteralExpr&>(expr).value));
                break;

            case ExprKind::FloatLiteral:
                Emit(OpCode::PushFloat, FloatConstant((f32)static_cast<const FloatLiteralExpr&>(expr).value));
                break;

            case ExprKind::BoolLiteral:
                Emit(OpCode::PushBool, static_cast<const BoolLiteralExpr&>(expr).value ? 1u : 0u);
                break;

            case ExprKind::StringLiteral:
                Emit(OpCode::PushString, StringConstant(static_cast<const StringLiteralExpr&>(expr).value));
                break;

            case ExprKind::Identifier:
                Emit(OpCode::LoadLocal, SlotOf(expr, "a variable reference"));
                break;

            case ExprKind::Unary: EmitUnary(static_cast<const UnaryExpr&>(expr)); break;
            case ExprKind::Binary: EmitBinary(static_cast<const BinaryExpr&>(expr)); break;
            case ExprKind::Assign: EmitAssign(static_cast<const AssignExpr&>(expr)); break;
            case ExprKind::Call: EmitCall(static_cast<const CallExpr&>(expr)); break;

            default:
                m_diagnostics.AddError(expr.location, "this expression cannot be turned into instructions");
                break;
        }

        EmitConversion(expr);
    }

    void EmitConversion(const Expr& expr)
    {
        if (expr.conversion == ValueType::Float)
        {
            Emit(OpCode::IntToFloat);
            return;
        }
        if (expr.conversion != ValueType::String) return;

        switch (expr.resolvedType)
        {
            case ValueType::Int: Emit(OpCode::IntToString); break;
            case ValueType::Float: Emit(OpCode::FloatToString); break;
            case ValueType::Bool: Emit(OpCode::BoolToString); break;
            default: break; // already text
        }
    }

    void EmitUnary(const UnaryExpr& expr)
    {
        EmitExpr(*expr.operand);
        if (expr.op == UnaryOp::Not)
        {
            Emit(OpCode::NotBool);
            return;
        }
        Emit(expr.resolvedType == ValueType::Float ? OpCode::NegFloat : OpCode::NegInt);
    }

    void EmitBinary(const BinaryExpr& expr)
    {
        // `&&` and `||` become jumps so the right-hand side is only
        // evaluated when it can still change the answer.
        if (expr.op == BinaryOp::LogicalAnd || expr.op == BinaryOp::LogicalOr)
        {
            const bool isAnd = expr.op == BinaryOp::LogicalAnd;
            EmitExpr(*expr.lhs);
            const u32 shortCircuit = EmitJump(isAnd ? OpCode::JumpIfFalse : OpCode::JumpIfTrue);
            EmitExpr(*expr.rhs);
            const u32 skipShortCircuit = EmitJump(OpCode::Jump);
            PatchJump(shortCircuit, Here());
            Emit(OpCode::PushBool, isAnd ? 0u : 1u);
            PatchJump(skipShortCircuit, Here());
            return;
        }

        EmitExpr(*expr.lhs);
        EmitExpr(*expr.rhs);

        switch (expr.op)
        {
            case BinaryOp::Equal:
            case BinaryOp::NotEqual:
            case BinaryOp::Less:
            case BinaryOp::Greater:
            case BinaryOp::LessEqual:
            case BinaryOp::GreaterEqual:
                Emit(ComparisonOpcode(expr.op, expr.operandType));
                break;
            default:
                Emit(ArithmeticOpcode(expr.op, expr.operandType));
                break;
        }
    }

    void EmitAssign(const AssignExpr& expr)
    {
        const u32 slot = SlotOf(*expr.target, "an assignment target");

        if (expr.op == AssignOp::Assign)
        {
            EmitExpr(*expr.value);
        }
        else
        {
            Emit(OpCode::LoadLocal, slot);
            EmitExpr(*expr.value);
            Emit(ArithmeticOpcode(BinaryOpForCompound(expr.op), expr.operandType));
        }

        Emit(OpCode::StoreLocal, slot);
        // An assignment is an expression, so it leaves the stored value
        // behind; a statement-level assignment discards it with a Pop.
        Emit(OpCode::LoadLocal, slot);
    }

    void EmitCall(const CallExpr& expr)
    {
        for (const ExprPtr& arg : expr.args) EmitExpr(*arg);

        if (expr.target == CallTarget::Native)
        {
            Emit(OpCode::CallNative, expr.targetIndex);
            return;
        }
        if (expr.target == CallTarget::ScriptMethod)
        {
            Emit(OpCode::Call, expr.targetIndex);
            return;
        }
        m_diagnostics.AddError(expr.location, "this call was never bound to a method");
    }
};

} // namespace

BytecodeModule Emit(const Program& program, const std::string& sourceName, u32 moduleVersion, DiagnosticList& diagnostics)
{
    EmitterState emitter(program, sourceName, moduleVersion, diagnostics);
    return emitter.Run();
}

} // namespace Fluxion::Script
