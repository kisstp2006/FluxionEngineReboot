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

#include <Compiler/AstClone.hpp>

#include <utility>

namespace Fluxion::Script
{

namespace
{

ExprPtr CloneExpr(const Expr* source);
StmtPtr CloneStmt(const Stmt* source);

// Copies what every node carries, so each clone function only has to deal
// with the parts that are its own.
void CopyExprBase(const Expr& source, Expr& target)
{
    target.location = source.location;
    target.resolvedType = source.resolvedType;
    target.resolvedClass = source.resolvedClass;
    target.conversion = source.conversion;
}

std::vector<ExprPtr> CloneExprList(const std::vector<ExprPtr>& source)
{
    std::vector<ExprPtr> cloned;
    cloned.reserve(source.size());
    for (const ExprPtr& entry : source) cloned.push_back(CloneExpr(entry.get()));
    return cloned;
}

template <typename NodeType>
std::unique_ptr<NodeType> MakeExprClone(const Expr& source)
{
    auto clone = std::make_unique<NodeType>();
    CopyExprBase(source, *clone);
    return clone;
}

ExprPtr CloneExpr(const Expr* source)
{
    if (!source) return nullptr;

    switch (source->kind)
    {
        case ExprKind::IntLiteral: {
            auto clone = MakeExprClone<IntLiteralExpr>(*source);
            clone->value = static_cast<const IntLiteralExpr*>(source)->value;
            return clone;
        }
        case ExprKind::FloatLiteral: {
            auto clone = MakeExprClone<FloatLiteralExpr>(*source);
            clone->value = static_cast<const FloatLiteralExpr*>(source)->value;
            return clone;
        }
        case ExprKind::BoolLiteral: {
            auto clone = MakeExprClone<BoolLiteralExpr>(*source);
            clone->value = static_cast<const BoolLiteralExpr*>(source)->value;
            return clone;
        }
        case ExprKind::StringLiteral: {
            auto clone = MakeExprClone<StringLiteralExpr>(*source);
            clone->value = static_cast<const StringLiteralExpr*>(source)->value;
            return clone;
        }
        case ExprKind::NullLiteral: return MakeExprClone<NullLiteralExpr>(*source);
        case ExprKind::This: return MakeExprClone<ThisExpr>(*source);

        case ExprKind::New: {
            const auto& node = *static_cast<const NewExpr*>(source);
            auto clone = MakeExprClone<NewExpr>(*source);
            clone->type = node.type;
            clone->args = CloneExprList(node.args);
            clone->classIndex = node.classIndex;
            clone->constructorFunction = node.constructorFunction;
            return clone;
        }

        case ExprKind::NewArray: {
            const auto& node = *static_cast<const NewArrayExpr*>(source);
            auto clone = MakeExprClone<NewArrayExpr>(*source);
            clone->elementType = node.elementType;
            clone->length = CloneExpr(node.length.get());
            clone->arrayClass = node.arrayClass;
            return clone;
        }

        case ExprKind::Identifier: {
            const auto& node = *static_cast<const IdentifierExpr*>(source);
            auto clone = MakeExprClone<IdentifierExpr>(*source);
            clone->name = node.name;
            clone->localSlot = node.localSlot;
            clone->isField = node.isField;
            clone->fieldSlot = node.fieldSlot;
            clone->isReadOnly = node.isReadOnly;
            return clone;
        }

        case ExprKind::Member: {
            const auto& node = *static_cast<const MemberExpr*>(source);
            auto clone = MakeExprClone<MemberExpr>(*source);
            clone->base = CloneExpr(node.base.get());
            clone->member = node.member;
            clone->typeArgs = node.typeArgs;
            clone->typeArgLocation = node.typeArgLocation;
            clone->binding = node.binding;
            clone->fieldSlot = node.fieldSlot;
            clone->constantValue = node.constantValue;
            return clone;
        }

        case ExprKind::Index: {
            const auto& node = *static_cast<const IndexExpr*>(source);
            auto clone = MakeExprClone<IndexExpr>(*source);
            clone->base = CloneExpr(node.base.get());
            clone->index = CloneExpr(node.index.get());
            return clone;
        }

        case ExprKind::Call: {
            const auto& node = *static_cast<const CallExpr*>(source);
            auto clone = MakeExprClone<CallExpr>(*source);
            clone->callee = CloneExpr(node.callee.get());
            clone->args = CloneExprList(node.args);
            clone->receiver = CloneExpr(node.receiver.get());
            clone->target = node.target;
            clone->targetIndex = node.targetIndex;
            clone->boundType = node.boundType;
            clone->boundMethod = node.boundMethod;
            return clone;
        }

        case ExprKind::Unary: {
            const auto& node = *static_cast<const UnaryExpr*>(source);
            auto clone = MakeExprClone<UnaryExpr>(*source);
            clone->op = node.op;
            clone->operand = CloneExpr(node.operand.get());
            return clone;
        }

        case ExprKind::Convert: {
            const auto& node = *static_cast<const ConvertExpr*>(source);
            auto clone = MakeExprClone<ConvertExpr>(*source);
            clone->target = node.target;
            clone->targetLocation = node.targetLocation;
            clone->operand = CloneExpr(node.operand.get());
            return clone;
        }

        case ExprKind::Binary: {
            const auto& node = *static_cast<const BinaryExpr*>(source);
            auto clone = MakeExprClone<BinaryExpr>(*source);
            clone->op = node.op;
            clone->lhs = CloneExpr(node.lhs.get());
            clone->rhs = CloneExpr(node.rhs.get());
            clone->operandType = node.operandType;
            return clone;
        }

        case ExprKind::Assign: {
            const auto& node = *static_cast<const AssignExpr*>(source);
            auto clone = MakeExprClone<AssignExpr>(*source);
            clone->op = node.op;
            clone->target = CloneExpr(node.target.get());
            clone->value = CloneExpr(node.value.get());
            clone->operandType = node.operandType;
            return clone;
        }
    }

    return nullptr;
}

template <typename NodeType>
std::unique_ptr<NodeType> MakeStmtClone(const Stmt& source)
{
    auto clone = std::make_unique<NodeType>();
    clone->location = source.location;
    return clone;
}

StmtPtr CloneStmt(const Stmt* source)
{
    if (!source) return nullptr;

    switch (source->kind)
    {
        case StmtKind::LocalDecl: {
            const auto& node = *static_cast<const LocalDeclStmt*>(source);
            auto clone = MakeStmtClone<LocalDeclStmt>(*source);
            clone->inferred = node.inferred;
            clone->declaredType = node.declaredType;
            clone->name = node.name;
            clone->initializer = CloneExpr(node.initializer.get());
            clone->localSlot = node.localSlot;
            return clone;
        }

        case StmtKind::Expr: {
            const auto& node = *static_cast<const ExprStmt*>(source);
            auto clone = MakeStmtClone<ExprStmt>(*source);
            clone->expr = CloneExpr(node.expr.get());
            return clone;
        }

        case StmtKind::Block: {
            const auto& node = *static_cast<const BlockStmt*>(source);
            auto clone = MakeStmtClone<BlockStmt>(*source);
            clone->statements.reserve(node.statements.size());
            for (const StmtPtr& child : node.statements) clone->statements.push_back(CloneStmt(child.get()));
            return clone;
        }

        case StmtKind::If: {
            const auto& node = *static_cast<const IfStmt*>(source);
            auto clone = MakeStmtClone<IfStmt>(*source);
            clone->condition = CloneExpr(node.condition.get());
            clone->thenBranch = CloneStmt(node.thenBranch.get());
            clone->elseBranch = CloneStmt(node.elseBranch.get());
            return clone;
        }

        case StmtKind::While: {
            const auto& node = *static_cast<const WhileStmt*>(source);
            auto clone = MakeStmtClone<WhileStmt>(*source);
            clone->condition = CloneExpr(node.condition.get());
            clone->body = CloneStmt(node.body.get());
            return clone;
        }

        case StmtKind::For: {
            const auto& node = *static_cast<const ForStmt*>(source);
            auto clone = MakeStmtClone<ForStmt>(*source);
            clone->init = CloneStmt(node.init.get());
            clone->condition = CloneExpr(node.condition.get());
            clone->step = CloneExpr(node.step.get());
            clone->body = CloneStmt(node.body.get());
            return clone;
        }

        case StmtKind::ForEach: {
            const auto& node = *static_cast<const ForEachStmt*>(source);
            auto clone = MakeStmtClone<ForEachStmt>(*source);
            clone->declaredType = node.declaredType;
            clone->name = node.name;
            clone->sequence = CloneExpr(node.sequence.get());
            clone->body = CloneStmt(node.body.get());
            return clone;
        }

        case StmtKind::Return: {
            const auto& node = *static_cast<const ReturnStmt*>(source);
            auto clone = MakeStmtClone<ReturnStmt>(*source);
            clone->value = CloneExpr(node.value.get());
            return clone;
        }

        case StmtKind::Break: return MakeStmtClone<BreakStmt>(*source);
        case StmtKind::Continue: return MakeStmtClone<ContinueStmt>(*source);
    }

    return nullptr;
}

std::unique_ptr<MethodDecl> CloneMethodDecl(const MethodDecl& source)
{
    auto clone = std::make_unique<MethodDecl>();
    clone->location = source.location;
    clone->returnType = source.returnType;
    clone->name = source.name;
    clone->params = source.params;
    clone->body = CloneStmt(source.body.get());

    clone->isStatic = source.isStatic;
    clone->isVirtual = source.isVirtual;
    clone->isOverride = source.isOverride;
    clone->isConstructor = source.isConstructor;

    clone->hasBaseCall = source.hasBaseCall;
    clone->baseArgs = CloneExprList(source.baseArgs);
    clone->baseCallLocation = source.baseCallLocation;
    return clone;
}

std::unique_ptr<FieldDecl> CloneFieldDecl(const FieldDecl& source)
{
    auto clone = std::make_unique<FieldDecl>();
    clone->location = source.location;
    clone->type = source.type;
    clone->name = source.name;
    clone->attributes = source.attributes;
    return clone;
}

// --- Substitution --------------------------------------------------------

using Substitution = std::unordered_map<std::string, TypeRef>;

void SubstituteType(TypeRef& type, const Substitution& substitution)
{
    for (TypeRef& argument : type.typeArgs) SubstituteType(argument, substitution);

    if (type.type != ValueType::Object || !type.typeArgs.empty()) return;

    auto found = substitution.find(type.name);
    if (found == substitution.end()) return;

    // `T[][]` keeps its brackets: only the element name is what the
    // parameter stood for.
    const u32 depth = type.arrayDepth;
    type = found->second;
    type.arrayDepth += depth;
}

void SubstituteExpr(Expr* expr, const Substitution& substitution);

void SubstituteStmt(Stmt* stmt, const Substitution& substitution)
{
    if (!stmt) return;

    switch (stmt->kind)
    {
        case StmtKind::LocalDecl: {
            auto& node = *static_cast<LocalDeclStmt*>(stmt);
            SubstituteType(node.declaredType, substitution);
            SubstituteExpr(node.initializer.get(), substitution);
            break;
        }
        case StmtKind::Expr:
            SubstituteExpr(static_cast<ExprStmt*>(stmt)->expr.get(), substitution);
            break;
        case StmtKind::Block:
            for (StmtPtr& child : static_cast<BlockStmt*>(stmt)->statements) SubstituteStmt(child.get(), substitution);
            break;
        case StmtKind::If: {
            auto& node = *static_cast<IfStmt*>(stmt);
            SubstituteExpr(node.condition.get(), substitution);
            SubstituteStmt(node.thenBranch.get(), substitution);
            SubstituteStmt(node.elseBranch.get(), substitution);
            break;
        }
        case StmtKind::While: {
            auto& node = *static_cast<WhileStmt*>(stmt);
            SubstituteExpr(node.condition.get(), substitution);
            SubstituteStmt(node.body.get(), substitution);
            break;
        }
        case StmtKind::For: {
            auto& node = *static_cast<ForStmt*>(stmt);
            SubstituteStmt(node.init.get(), substitution);
            SubstituteExpr(node.condition.get(), substitution);
            SubstituteExpr(node.step.get(), substitution);
            SubstituteStmt(node.body.get(), substitution);
            break;
        }
        case StmtKind::ForEach: {
            auto& node = *static_cast<ForEachStmt*>(stmt);
            SubstituteType(node.declaredType, substitution);
            SubstituteExpr(node.sequence.get(), substitution);
            SubstituteStmt(node.body.get(), substitution);
            break;
        }
        case StmtKind::Return:
            SubstituteExpr(static_cast<ReturnStmt*>(stmt)->value.get(), substitution);
            break;
        case StmtKind::Break:
        case StmtKind::Continue:
            break;
    }
}

void SubstituteExpr(Expr* expr, const Substitution& substitution)
{
    if (!expr) return;

    switch (expr->kind)
    {
        case ExprKind::New: {
            auto& node = *static_cast<NewExpr*>(expr);
            SubstituteType(node.type, substitution);
            for (ExprPtr& arg : node.args) SubstituteExpr(arg.get(), substitution);
            break;
        }
        case ExprKind::NewArray: {
            auto& node = *static_cast<NewArrayExpr*>(expr);
            SubstituteType(node.elementType, substitution);
            SubstituteExpr(node.length.get(), substitution);
            break;
        }
        case ExprKind::Member: {
            auto& node = *static_cast<MemberExpr*>(expr);
            SubstituteExpr(node.base.get(), substitution);
            for (TypeRef& typeArg : node.typeArgs) SubstituteType(typeArg, substitution);
            break;
        }
        case ExprKind::Index: {
            auto& node = *static_cast<IndexExpr*>(expr);
            SubstituteExpr(node.base.get(), substitution);
            SubstituteExpr(node.index.get(), substitution);
            break;
        }
        case ExprKind::Call: {
            auto& node = *static_cast<CallExpr*>(expr);
            SubstituteExpr(node.callee.get(), substitution);
            SubstituteExpr(node.receiver.get(), substitution);
            for (ExprPtr& arg : node.args) SubstituteExpr(arg.get(), substitution);
            break;
        }
        case ExprKind::Unary:
            SubstituteExpr(static_cast<UnaryExpr*>(expr)->operand.get(), substitution);
            break;
        case ExprKind::Convert:
            SubstituteExpr(static_cast<ConvertExpr*>(expr)->operand.get(), substitution);
            break;
        case ExprKind::Binary: {
            auto& node = *static_cast<BinaryExpr*>(expr);
            SubstituteExpr(node.lhs.get(), substitution);
            SubstituteExpr(node.rhs.get(), substitution);
            break;
        }
        case ExprKind::Assign: {
            auto& node = *static_cast<AssignExpr*>(expr);
            SubstituteExpr(node.target.get(), substitution);
            SubstituteExpr(node.value.get(), substitution);
            break;
        }
        default:
            break;
    }
}

} // namespace

std::unique_ptr<ClassDecl> CloneClassDecl(const ClassDecl& source)
{
    auto clone = std::make_unique<ClassDecl>();
    clone->location = source.location;
    clone->name = source.name;
    clone->isStatic = source.isStatic;
    clone->isInterface = source.isInterface;
    clone->isStruct = source.isStruct;
    clone->isEnum = source.isEnum;
    clone->enumerators = source.enumerators;
    clone->attributes = source.attributes;
    clone->typeParams = source.typeParams;
    clone->typeParamLocations = source.typeParamLocations;
    clone->baseTypes = source.baseTypes;
    clone->baseLocations = source.baseLocations;

    clone->fields.reserve(source.fields.size());
    for (const DeclPtr& field : source.fields)
        clone->fields.push_back(CloneFieldDecl(*static_cast<const FieldDecl*>(field.get())));

    clone->methods.reserve(source.methods.size());
    for (const DeclPtr& method : source.methods)
        clone->methods.push_back(CloneMethodDecl(*static_cast<const MethodDecl*>(method.get())));

    return clone;
}

void SubstituteTypeParameters(ClassDecl& target, const std::unordered_map<std::string, TypeRef>& substitution)
{
    for (TypeRef& baseType : target.baseTypes) SubstituteType(baseType, substitution);

    // An annotation naming a type parameter names whatever that parameter
    // stood for in this copy, exactly as a field's declared type does.
    for (AttributeNode& attribute : target.attributes)
    {
        for (AttributeArgNode& argument : attribute.args) SubstituteType(argument.typeValue, substitution);
    }

    for (DeclPtr& fieldDecl : target.fields)
    {
        auto& field = *static_cast<FieldDecl*>(fieldDecl.get());
        SubstituteType(field.type, substitution);
        for (AttributeNode& attribute : field.attributes)
        {
            for (AttributeArgNode& argument : attribute.args) SubstituteType(argument.typeValue, substitution);
        }
    }

    for (DeclPtr& methodDecl : target.methods)
    {
        auto& method = *static_cast<MethodDecl*>(methodDecl.get());
        SubstituteType(method.returnType, substitution);
        for (ParamDecl& param : method.params) SubstituteType(param.type, substitution);
        for (ExprPtr& arg : method.baseArgs) SubstituteExpr(arg.get(), substitution);
        SubstituteStmt(method.body.get(), substitution);
    }
}

} // namespace Fluxion::Script
