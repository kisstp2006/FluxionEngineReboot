#include "TestFramework.h"

#include <Fluxion/Script/Compiler/Lexer.hpp>
#include <Fluxion/Script/Compiler/Parser.hpp>

#include <string>

using namespace Fluxion::Script;

namespace
{

Program ParseSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    return Parse(tokens, diagnostics);
}

const ClassDecl* ClassAt(const Program& program, size_t index)
{
    if (index >= program.declarations.size()) return nullptr;
    if (program.declarations[index]->kind != DeclKind::Class) return nullptr;
    return static_cast<const ClassDecl*>(program.declarations[index].get());
}

const MethodDecl* MethodAt(const ClassDecl* classDecl, size_t index)
{
    if (!classDecl || index >= classDecl->methods.size()) return nullptr;
    if (classDecl->methods[index]->kind != DeclKind::Method) return nullptr;
    return static_cast<const MethodDecl*>(classDecl->methods[index].get());
}

const Stmt* StatementAt(const MethodDecl* method, size_t index)
{
    if (!method || !method->body || method->body->kind != StmtKind::Block) return nullptr;
    const auto* block = static_cast<const BlockStmt*>(method->body.get());
    if (index >= block->statements.size()) return nullptr;
    return block->statements[index].get();
}

// The expression of the first statement of the first method of the first
// class, which is where most of the shape assertions below look.
const Expr* FirstReturnedExpr(const Program& program)
{
    const Stmt* stmt = StatementAt(MethodAt(ClassAt(program, 0), 0), 0);
    if (!stmt || stmt->kind != StmtKind::Return) return nullptr;
    return static_cast<const ReturnStmt*>(stmt)->value.get();
}

} // namespace

void Test_Parser_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "static class Program\n"
            "{\n"
            "    static int Add(int a, int b) { return a + b; }\n"
            "    static void Main() { int x = 3; }\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);

        const ClassDecl* programClass = ClassAt(program, 0);
        TEST_CHECK(ctx, programClass != nullptr);
        TEST_CHECK(ctx, programClass && programClass->name == "Program");
        TEST_CHECK(ctx, programClass && programClass->methods.size() == 2);

        const MethodDecl* add = MethodAt(programClass, 0);
        TEST_CHECK(ctx, add && add->name == "Add");
        TEST_CHECK(ctx, add && add->returnType == ValueType::Int);
        TEST_CHECK(ctx, add && add->params.size() == 2);
        TEST_CHECK(ctx, add && add->params[0].type == ValueType::Int && add->params[0].name == "a");

        const MethodDecl* main = MethodAt(programClass, 1);
        TEST_CHECK(ctx, main && main->returnType == ValueType::Void);
        const Stmt* decl = StatementAt(main, 0);
        TEST_CHECK(ctx, decl && decl->kind == StmtKind::LocalDecl);
    }
    {
        // Multiplication binds tighter than addition, so the sum's right
        // operand is the product and not the other way round.
        DiagnosticList diagnostics;
        Program program = ParseSource("static class P { static int M() { return 1 + 2 * 3; } }", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        const Expr* expr = FirstReturnedExpr(program);
        TEST_CHECK(ctx, expr && expr->kind == ExprKind::Binary);
        if (expr && expr->kind == ExprKind::Binary)
        {
            const auto* sum = static_cast<const BinaryExpr*>(expr);
            TEST_CHECK(ctx, sum->op == BinaryOp::Add);
            TEST_CHECK(ctx, sum->lhs->kind == ExprKind::IntLiteral);
            TEST_CHECK(ctx, sum->rhs->kind == ExprKind::Binary);
            if (sum->rhs->kind == ExprKind::Binary)
                TEST_CHECK(ctx, static_cast<const BinaryExpr*>(sum->rhs.get())->op == BinaryOp::Mul);
        }
    }
    {
        // Comparison is looser than arithmetic: `a + 1 < b * 2` is one
        // comparison of two arithmetic results.
        DiagnosticList diagnostics;
        Program program = ParseSource("static class P { static int M() { return 1 + 1 < 2 * 2; } }", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        const Expr* expr = FirstReturnedExpr(program);
        TEST_CHECK(ctx, expr && expr->kind == ExprKind::Binary);
        if (expr && expr->kind == ExprKind::Binary)
        {
            const auto* comparison = static_cast<const BinaryExpr*>(expr);
            TEST_CHECK(ctx, comparison->op == BinaryOp::Less);
            TEST_CHECK(ctx, comparison->lhs->kind == ExprKind::Binary);
            TEST_CHECK(ctx, comparison->rhs->kind == ExprKind::Binary);
        }
    }
    {
        // Assignment groups to the right: `a = b = c` assigns the result
        // of `b = c` to a.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "static class P { static void M() { int a = 0; int b = 0; int c = 0; a = b = c; } }",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        const Stmt* stmt = StatementAt(MethodAt(ClassAt(program, 0), 0), 3);
        TEST_CHECK(ctx, stmt && stmt->kind == StmtKind::Expr);
        if (stmt && stmt->kind == StmtKind::Expr)
        {
            const Expr* expr = static_cast<const ExprStmt*>(stmt)->expr.get();
            TEST_CHECK(ctx, expr->kind == ExprKind::Assign);
            if (expr->kind == ExprKind::Assign)
            {
                const auto* outer = static_cast<const AssignExpr*>(expr);
                TEST_CHECK(ctx, outer->target->kind == ExprKind::Identifier);
                TEST_CHECK(ctx, static_cast<const IdentifierExpr*>(outer->target.get())->name == "a");
                TEST_CHECK(ctx, outer->value->kind == ExprKind::Assign);
            }
        }
    }
    {
        // A qualified call parses as a call whose callee is a member
        // access.
        DiagnosticList diagnostics;
        Program program = ParseSource("static class P { static void M() { Console.WriteLine(\"hi\"); } }", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        const Stmt* stmt = StatementAt(MethodAt(ClassAt(program, 0), 0), 0);
        TEST_CHECK(ctx, stmt && stmt->kind == StmtKind::Expr);
        if (stmt && stmt->kind == StmtKind::Expr)
        {
            const Expr* expr = static_cast<const ExprStmt*>(stmt)->expr.get();
            TEST_CHECK(ctx, expr->kind == ExprKind::Call);
            if (expr->kind == ExprKind::Call)
            {
                const auto* call = static_cast<const CallExpr*>(expr);
                TEST_CHECK(ctx, call->args.size() == 1);
                TEST_CHECK(ctx, call->callee->kind == ExprKind::Member);
                if (call->callee->kind == ExprKind::Member)
                    TEST_CHECK(ctx, static_cast<const MemberExpr*>(call->callee.get())->member == "WriteLine");
            }
        }
    }
    {
        // Every control-flow form parses into its own node.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "static class P\n"
            "{\n"
            "    static void M()\n"
            "    {\n"
            "        if (true) { } else { }\n"
            "        while (true) { break; }\n"
            "        for (int i = 0; i < 3; i += 1) { continue; }\n"
            "        return;\n"
            "    }\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        const MethodDecl* method = MethodAt(ClassAt(program, 0), 0);
        TEST_CHECK(ctx, StatementAt(method, 0) && StatementAt(method, 0)->kind == StmtKind::If);
        TEST_CHECK(ctx, StatementAt(method, 1) && StatementAt(method, 1)->kind == StmtKind::While);
        TEST_CHECK(ctx, StatementAt(method, 2) && StatementAt(method, 2)->kind == StmtKind::For);
        TEST_CHECK(ctx, StatementAt(method, 3) && StatementAt(method, 3)->kind == StmtKind::Return);
    }
    {
        // A malformed member is reported once, and the declaration after
        // it still parses -- one mistake must not swallow the rest of the
        // file.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "static class Broken { static int 5() { return 1; } }\n"
            "static class Good { static int G() { return 2; } }\n",
            diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 2);

        const ClassDecl* recovered = ClassAt(program, 1);
        TEST_CHECK(ctx, recovered && recovered->name == "Good");
        TEST_CHECK(ctx, recovered && recovered->methods.size() == 1);
        TEST_CHECK(ctx, MethodAt(recovered, 0) && MethodAt(recovered, 0)->name == "G");
    }
    {
        // A broken class as a whole is skipped, and the next one is still
        // found.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "static class 9Bad { }\n"
            "static class Good { static int G() { return 2; } }\n",
            diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);
        TEST_CHECK(ctx, ClassAt(program, 0) && ClassAt(program, 0)->name == "Good");
    }
}
