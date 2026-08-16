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
        case ValueType::Object:
        case ValueType::Null:
            return op == BinaryOp::Equal ? OpCode::EqualRef : OpCode::NotEqualRef;
        // A named constant is the number it stands for, so comparing two
        // of them is comparing two whole numbers.
        case ValueType::Enum:
            return op == BinaryOp::Equal ? OpCode::EqualInt : OpCode::NotEqualInt;
        case ValueType::Handle:
            return op == BinaryOp::Equal ? OpCode::EqualHandle : OpCode::NotEqualHandle;
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
    EmitterState(const Program& program, std::string sourceName, u32 moduleVersion, DiagnosticList& diagnostics,
        const BindingTable* bindings)
        : m_program(program), m_sourceName(std::move(sourceName)), m_moduleVersion(moduleVersion), m_diagnostics(diagnostics),
          m_bindings(bindings)
    {
    }

    BytecodeModule Run()
    {
        m_module.sourceName = m_sourceName;
        m_module.header.moduleVersion = m_moduleVersion;

        std::vector<const ClassDecl*> classes;
        std::vector<const MethodDecl*> methods;
        for (const DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Class) continue;
            auto* classDecl = static_cast<ClassDecl*>(decl.get());

            // A declaration written with type parameters was never given
            // an index, because it is a pattern rather than a type: what
            // reaches the module is each concrete copy made from it.
            if (classDecl->classIndex == kNoClass) continue;

            classes.push_back(classDecl);
            for (const DeclPtr& methodDecl : classDecl->methods)
                methods.push_back(static_cast<MethodDecl*>(methodDecl.get()));
        }

        EmitClassTable(classes);

        m_module.functions.resize(methods.size());
        for (const MethodDecl* method : methods)
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
    const BindingTable* m_bindings = nullptr;

    BytecodeModule m_module;

    // Instructions of the function currently being emitted; appended to
    // the module's flat stream once complete, which is what makes jump
    // targets function-relative. `m_lines` runs alongside it, one entry
    // per instruction.
    std::vector<Instruction> m_code;
    std::vector<u32> m_lines;

    // What the method being emitted runs on and answers with, counted in
    // slots. A method on a value type runs on a copy that lives in the
    // frame's lowest slots, which is what makes `this` addressable there
    // and what a constructor of one hands back when it is done.
    u32 m_receiverSlots = 0;
    bool m_receiverIsValue = false;
    u32 m_returnSlots = 0;
    bool m_valueConstructor = false;

    // Which line the instructions being emitted right now came from. A
    // node sets it while it is being walked and hands it back afterwards,
    // so an instruction a construct emits after its operands still
    // belongs to the construct and not to the last operand.
    u32 m_line = 0;

    class LineScope
    {
    public:
        LineScope(EmitterState& state, u32 line) : m_state(state), m_previous(state.m_line)
        {
            if (line != 0) m_state.m_line = line;
        }

        ~LineScope() { m_state.m_line = m_previous; }

        LineScope(const LineScope&) = delete;
        LineScope& operator=(const LineScope&) = delete;

    private:
        EmitterState& m_state;
        u32 m_previous;
    };

    struct LoopContext
    {
        std::vector<u32> breakSites;
        std::vector<u32> continueSites;
    };
    std::vector<LoopContext> m_loops;

    std::unordered_map<i32, u32> m_intConstantIndices;
    std::unordered_map<u32, u32> m_floatConstantIndices; // keyed by bit pattern, so -0 and 0 stay distinct
    std::unordered_map<std::string, u32> m_stringConstantIndices;

    // What an annotation looked like once the analyzer was done with it.
    // Nothing is resolved here: a `typeof` already carries the class it
    // named, and a number already knows which kind it is.
    static std::vector<Attribute> BuildAttributes(const std::vector<AttributeNode>& written)
    {
        std::vector<Attribute> built;
        built.reserve(written.size());

        for (const AttributeNode& node : written)
        {
            Attribute attribute;
            attribute.name = node.name;
            attribute.arguments.reserve(node.args.size());

            for (const AttributeArgNode& argument : node.args)
            {
                AttributeArgument value;
                value.kind = argument.kind;
                switch (argument.kind)
                {
                    case AttributeArgumentKind::Int:
                        value.intValue = (i32)argument.intValue;
                        // A whole number is also readable as one, so an
                        // annotation taking a number does not have to
                        // care which way it was written.
                        value.floatValue = (f32)argument.intValue;
                        break;
                    case AttributeArgumentKind::Float:
                        value.floatValue = (f32)argument.floatValue;
                        value.intValue = (i32)argument.floatValue;
                        break;
                    case AttributeArgumentKind::String:
                        value.stringValue = argument.stringValue;
                        break;
                    case AttributeArgumentKind::Class:
                        value.classIndex = argument.classIndex;
                        break;
                }
                attribute.arguments.push_back(std::move(value));
            }
            built.push_back(std::move(attribute));
        }
        return built;
    }

    static std::vector<FieldInfo> BuildFields(const ClassDecl& classDecl)
    {
        std::vector<FieldInfo> built;
        built.reserve(classDecl.fields.size());

        for (const DeclPtr& fieldDecl : classDecl.fields)
        {
            const auto& field = *static_cast<const FieldDecl*>(fieldDecl.get());

            FieldInfo info;
            info.name = field.name;
            info.type = field.type.type;
            info.typeClass = field.type.classIndex;
            info.slot = field.fieldSlot;
            info.attributes = BuildAttributes(field.attributes);
            built.push_back(std::move(info));
        }
        return built;
    }

    void EmitClassTable(const std::vector<const ClassDecl*>& classes)
    {
        u32 count = 0;
        for (const ClassDecl* classDecl : classes)
        {
            if (classDecl->classIndex == kNoClass) continue;
            if (classDecl->classIndex + 1 > count) count = classDecl->classIndex + 1;
        }
        m_module.classes.resize(count);

        for (const ClassDecl* classDecl : classes)
        {
            if (classDecl->classIndex >= m_module.classes.size())
            {
                m_diagnostics.AddError(classDecl->location, "'" + classDecl->name + "' was not assigned a valid slot in the module");
                continue;
            }

            ClassInfo& info = m_module.classes[classDecl->classIndex];
            info.name = classDecl->name;
            info.baseClass = classDecl->baseClass;
            info.isInterface = classDecl->isInterface;
            info.isStatic = classDecl->isStatic;
            info.isStruct = classDecl->isStruct;
            info.isEnum = classDecl->isEnum;
            info.isArray = classDecl->isArray;
            info.elementIsReference = classDecl->arrayElementIsReference;
            info.elementSlotCount = classDecl->arrayElementSlotCount != 0 ? classDecl->arrayElementSlotCount : 1;
            info.fieldSlotCount = classDecl->fieldSlotCount;
            info.fieldReferenceBits = classDecl->fieldReferenceBits;
            info.vtable = classDecl->vtable;
            info.interfaces = classDecl->interfaceTables;
            info.constructorFunction = classDecl->constructorFunction;
            info.attributes = BuildAttributes(classDecl->attributes);
            info.fields = BuildFields(*classDecl);
        }
    }

    void EmitMethod(const MethodDecl& method, FunctionInfo& outInfo)
    {
        m_code.clear();
        m_lines.clear();
        m_loops.clear();
        m_line = method.location.line;

        m_receiverSlots = method.receiverSlotCount;
        m_receiverIsValue = method.receiverIsValue;
        m_returnSlots = method.returnSlotCount;
        m_valueConstructor = method.isConstructor && method.receiverIsValue;

        outInfo.qualifiedName = method.qualifiedName;
        outInfo.sourceFile = method.location.file;

        // A constructor of a value type answers with the value, not with
        // nothing: there is no object the caller could have kept a name
        // for, so what it made is what comes back.
        outInfo.returnType = m_valueConstructor ? ValueType::Struct : method.returnType.type;
        outInfo.parameterTypes.clear();
        for (const ParamDecl& param : method.params) outInfo.parameterTypes.push_back(param.type.type);

        outInfo.returnSlotCount = method.returnSlotCount;
        outInfo.parameterSlotCount = method.parameterSlotCount;
        outInfo.receiverSlotCount = method.receiverSlotCount;
        outInfo.receiverIsValue = method.receiverIsValue;

        outInfo.owningClass = method.owningClass;
        outInfo.vtableSlot = method.vtableSlot;
        outInfo.isInstance = IsInstanceMethod(method);

        outInfo.localReferenceBits.clear();
        for (u32 slot = 0; slot < (u32)method.localIsReference.size(); ++slot)
        {
            if (method.localIsReference[slot] != 0) SetReferenceBit(outInfo.localReferenceBits, slot);
        }

        // A frame is at least large enough for the receiver and the
        // parameters, whatever the body turned out to need.
        const u32 fixedSlots = method.parameterSlotCount + method.receiverSlotCount;
        outInfo.localSlotCount = method.localSlotCount < fixedSlots ? fixedSlots : method.localSlotCount;

        // A method an interface only declares carries a signature and no
        // instructions: a call site names it to say what it is calling,
        // and the receiver's own class supplies the body.
        if (!method.body)
        {
            outInfo.hasBody = false;
            outInfo.codeOffset = (u32)m_module.code.size();
            outInfo.codeLength = 0;
            return;
        }

        if (method.isConstructor) EmitBaseConstructorCall(method);

        EmitStmt(*method.body);

        // Every function ends with an explicit return instruction, even
        // when the body already returned on every path. A construct that
        // sits last in a body patches its exit jump to "one past the last
        // instruction emitted so far", and this trailing return is what
        // guarantees that landing site exists.
        EmitDefaultReturn(method.returnType.type);

        outInfo.hasBody = true;
        outInfo.codeOffset = (u32)m_module.code.size();
        outInfo.codeLength = (u32)m_code.size();
        m_module.code.insert(m_module.code.end(), m_code.begin(), m_code.end());
        m_module.sourceLines.insert(m_module.sourceLines.end(), m_lines.begin(), m_lines.end());
    }

    static bool IsInstanceMethod(const MethodDecl& method)
    {
        return !method.isStatic && method.owningClass != kNoClass;
    }

    // A constructor's first act is to finish building the part of the
    // object its base class owns, so a base method called from here
    // already sees the values it expects.
    void EmitBaseConstructorCall(const MethodDecl& method)
    {
        if (method.baseConstructorFunction == kNoFunction) return;

        Emit(OpCode::LoadLocal, 0);
        for (const ExprPtr& arg : method.baseArgs) EmitExpr(*arg);
        Emit(OpCode::Call, method.baseConstructorFunction);
    }

    void EmitDefaultReturn(ValueType returnType)
    {
        // A constructor of a value type ends by handing back the slots it
        // was given, however its body left them.
        if (m_valueConstructor)
        {
            EmitReturnReceiver();
            return;
        }

        switch (returnType)
        {
            case ValueType::Int: Emit(OpCode::PushInt, IntConstant(0)); Emit(OpCode::Return); break;
            case ValueType::Float: Emit(OpCode::PushFloat, FloatConstant(0.0f)); Emit(OpCode::Return); break;
            case ValueType::Bool: Emit(OpCode::PushBool, 0); Emit(OpCode::Return); break;
            case ValueType::String: Emit(OpCode::PushString, StringConstant(std::string())); Emit(OpCode::Return); break;
            // A named constant starts at zero exactly as a number does.
            case ValueType::Enum: Emit(OpCode::PushInt, IntConstant(0)); Emit(OpCode::Return); break;
            // A value type starts out with every field at its own zero,
            // which is a run of zeroed slots.
            case ValueType::Struct:
                Emit(OpCode::PushZero, m_returnSlots);
                Emit(OpCode::ReturnWide, m_returnSlots);
                break;
            // An all-zero slot is the null reference, and is equally the
            // handle that names nothing: neither needs an opcode of its
            // own to produce.
            case ValueType::Object:
            case ValueType::Handle: Emit(OpCode::PushNull); Emit(OpCode::Return); break;
            default: Emit(OpCode::ReturnVoid); break;
        }
    }

    void EmitReturnReceiver()
    {
        Emit(OpCode::LoadLocalWide, MakeSlotSpan(0, m_receiverSlots));
        Emit(OpCode::ReturnWide, m_receiverSlots);
    }

    // Hands back whatever is already on the stack, which is one slot for
    // most types and a run of them for a value type.
    void EmitReturnValue()
    {
        if (m_returnSlots > 1) Emit(OpCode::ReturnWide, m_returnSlots);
        else Emit(OpCode::Return);
    }

    // --- Emission helpers ---------------------------------------------------

    u32 Here() const { return (u32)m_code.size(); }

    void Emit(OpCode op, u32 operand = 0)
    {
        m_code.push_back(Instruction{ op, operand });
        m_lines.push_back(m_line);
    }

    u32 EmitJump(OpCode op)
    {
        Emit(op, 0);
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

    // --- Values wider than one slot -----------------------------------------

    // How many slots a value of this type occupies. Everything answers
    // one except a value type, whose width is the field area the class
    // table already records for it.
    u32 SlotWidth(ValueType type, u32 classIndex) const
    {
        if (type != ValueType::Struct) return 1;
        if (classIndex >= m_module.classes.size()) return 1;
        const u32 width = m_module.classes[classIndex].fieldSlotCount;
        return width == 0 ? 1 : width;
    }

    u32 SlotWidth(const Expr& expr) const { return SlotWidth(expr.resolvedType, expr.resolvedClass); }
    u32 SlotWidth(const TypeRef& type) const { return SlotWidth(type.type, type.classIndex); }

    u32 ElementWidth(const Expr& sequence) const
    {
        if (sequence.resolvedClass >= m_module.classes.size()) return 1;
        const u32 width = m_module.classes[sequence.resolvedClass].elementSlotCount;
        return width == 0 ? 1 : width;
    }

    // Where a value lives, so that reading one field of it and writing
    // one field of it both come out as instructions that touch exactly
    // those slots and no others. A field of a value type is not a step
    // through anything at run time: it is an offset added to wherever the
    // value itself sits, which is what a chain of them collapses into
    // here.
    struct ValuePath
    {
        enum class Kind
        {
            // Frame slots, an object's field slots, one element's slots,
            // and -- when the value belongs to no storage at all -- the
            // slots the expression itself leaves behind.
            Local,
            Field,
            Element,
            Stack,
        };

        Kind kind = Kind::Stack;

        const Expr* object = nullptr; // Field: null means the enclosing instance
        const Expr* array = nullptr;  // Element
        const Expr* index = nullptr;
        const Expr* source = nullptr; // Stack

        u32 slot = 0;  // offset of the wanted run within the storage
        u32 width = 1; // how long the wanted run is
        u32 total = 1; // Stack: how much the expression produces altogether
    };

    ValuePath ResolvePath(const Expr& expr) const
    {
        ValuePath path;
        path.width = SlotWidth(expr);

        switch (expr.kind)
        {
            case ExprKind::Identifier: {
                const auto& identifier = static_cast<const IdentifierExpr&>(expr);
                if (identifier.isField)
                {
                    path.kind = ValuePath::Kind::Field;
                    path.slot = identifier.fieldSlot;
                    return path;
                }
                if (identifier.localSlot >= 0)
                {
                    path.kind = ValuePath::Kind::Local;
                    path.slot = (u32)identifier.localSlot;
                    return path;
                }
                break;
            }

            case ExprKind::This: {
                // A value-type receiver is storage of its own -- the
                // frame's lowest slots. An object receiver is a reference
                // like any other, and reaches its fields through itself.
                if (!m_receiverIsValue) break;
                path.kind = ValuePath::Kind::Local;
                path.slot = 0;
                return path;
            }

            case ExprKind::Index: {
                const auto& node = static_cast<const IndexExpr&>(expr);
                path.kind = ValuePath::Kind::Element;
                path.array = node.base.get();
                path.index = node.index.get();
                return path;
            }

            case ExprKind::Member: {
                const auto& member = static_cast<const MemberExpr&>(expr);
                if (member.binding != MemberBinding::Field || !member.base) break;

                if (member.base->resolvedType != ValueType::Struct)
                {
                    path.kind = ValuePath::Kind::Field;
                    path.object = member.base.get();
                    path.slot = member.fieldSlot;
                    return path;
                }

                ValuePath base = ResolvePath(*member.base);
                base.slot += member.fieldSlot;
                base.width = path.width;
                return base;
            }

            default: break;
        }

        path.kind = ValuePath::Kind::Stack;
        path.source = &expr;
        path.total = path.width;
        return path;
    }

    void EmitLoadRun(OpCode narrow, OpCode wide, u32 slot, u32 width)
    {
        if (width == 1) Emit(narrow, slot);
        else Emit(wide, MakeSlotSpan(slot, width));
    }

    void EmitStoreRun(OpCode narrow, OpCode wide, u32 slot, u32 width)
    {
        if (width == 1) Emit(narrow, slot);
        else Emit(wide, MakeSlotSpan(slot, width));
    }

    void EmitPathReceiver(const ValuePath& path)
    {
        if (path.object) EmitExpr(*path.object);
        else Emit(OpCode::LoadLocal, 0);
    }

    // Keeps `width` slots starting at `offset` out of a value of `total`
    // slots that is already on the stack: what sits above them is popped,
    // and what sits below them is dropped out from under them.
    void EmitNarrow(u32 total, u32 offset, u32 width)
    {
        for (u32 i = offset + width; i < total; ++i) Emit(OpCode::Pop);
        if (offset != 0) Emit(OpCode::DiscardUnder, MakeSlotSpan(offset, width));
    }

    void EmitLoadPath(const ValuePath& path)
    {
        switch (path.kind)
        {
            case ValuePath::Kind::Local:
                EmitLoadRun(OpCode::LoadLocal, OpCode::LoadLocalWide, path.slot, path.width);
                return;

            case ValuePath::Kind::Field:
                EmitPathReceiver(path);
                EmitLoadRun(OpCode::LoadField, OpCode::LoadFieldWide, path.slot, path.width);
                return;

            case ValuePath::Kind::Element:
                EmitExpr(*path.array);
                EmitExpr(*path.index);
                EmitElementAccess(*path.array, path, false);
                return;

            case ValuePath::Kind::Stack:
                EmitExpr(*path.source);
                EmitNarrow(path.total, path.slot, path.width);
                return;
        }
    }

    // A whole element is what the sequence type says it is, so reading or
    // writing one needs no width in the instruction; a field of one
    // carries where it sits and how long it is.
    void EmitElementAccess(const Expr& sequence, const ValuePath& path, bool storing)
    {
        const u32 elementWidth = ElementWidth(sequence);
        const bool whole = path.slot == 0 && path.width == elementWidth;
        if (whole)
        {
            // The whole element carries its width, which the machine
            // checks against the sequence type rather than trusting.
            Emit(storing ? OpCode::StoreElement : OpCode::LoadElement, elementWidth);
            return;
        }
        Emit(storing ? OpCode::StoreElementField : OpCode::LoadElementField, MakeSlotSpan(path.slot, path.width));
    }

    // --- Statements ---------------------------------------------------------

    void EmitStmt(const Stmt& stmt)
    {
        LineScope line(*this, stmt.location.line);

        switch (stmt.kind)
        {
            case StmtKind::LocalDecl: {
                const auto& node = static_cast<const LocalDeclStmt&>(stmt);
                if (!node.initializer) break; // frame slots start zeroed
                EmitExpr(*node.initializer);
                EmitStoreRun(OpCode::StoreLocal, OpCode::StoreLocalWide,
                    node.localSlot >= 0 ? (u32)node.localSlot : 0, SlotWidth(node.declaredType));
                break;
            }

            case StmtKind::Expr: {
                const auto& node = static_cast<const ExprStmt&>(stmt);
                EmitExpr(*node.expr);
                EmitDiscard(*node.expr);
                break;
            }

            case StmtKind::Block: {
                const auto& node = static_cast<const BlockStmt&>(stmt);
                for (size_t i = 0; i < node.statements.size(); ++i)
                {
                    // The gap between two statements is a place where
                    // nothing is half-computed, which is the only kind of
                    // place a collection is allowed to happen.
                    if (i != 0) Emit(OpCode::SafePoint);
                    EmitStmt(*node.statements[i]);
                }
                break;
            }

            case StmtKind::If: EmitIf(static_cast<const IfStmt&>(stmt)); break;
            case StmtKind::While: EmitWhile(static_cast<const WhileStmt&>(stmt)); break;
            case StmtKind::For: EmitFor(static_cast<const ForStmt&>(stmt)); break;
            case StmtKind::ForEach: EmitForEach(static_cast<const ForEachStmt&>(stmt)); break;

            case StmtKind::Return: {
                const auto& node = static_cast<const ReturnStmt&>(stmt);
                if (node.value)
                {
                    EmitExpr(*node.value);
                    EmitReturnValue();
                }
                else if (m_valueConstructor)
                {
                    EmitReturnReceiver();
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
        // The top of a loop is reached once per iteration with nothing
        // pending, so a loop that allocates always passes somewhere a
        // collection can run even when its body is a single statement.
        const u32 conditionLabel = Here();
        Emit(OpCode::SafePoint);
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
        Emit(OpCode::SafePoint);
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
            EmitDiscard(*stmt.step);
        }
        Emit(OpCode::Jump, conditionLabel);

        const u32 endLabel = Here();
        if (hasExitJump) PatchJump(exitJump, endLabel);
        CloseLoop(stepLabel, endLabel);
    }

    // Walking a sequence is a counted loop over its positions, written
    // out here rather than represented at run time: there is no iterator
    // to create, and the loop variable is simply the slot each turn
    // writes the element it reached into.
    void EmitForEach(const ForEachStmt& stmt)
    {
        const u32 sequenceSlot = stmt.sequenceSlot >= 0 ? (u32)stmt.sequenceSlot : 0;
        const u32 positionSlot = stmt.indexSlot >= 0 ? (u32)stmt.indexSlot : 0;
        const u32 elementSlot = stmt.loopSlot >= 0 ? (u32)stmt.loopSlot : 0;

        EmitExpr(*stmt.sequence);
        Emit(OpCode::StoreLocal, sequenceSlot);
        Emit(OpCode::PushInt, IntConstant(0));
        Emit(OpCode::StoreLocal, positionSlot);

        const u32 conditionLabel = Here();
        Emit(OpCode::SafePoint);
        Emit(OpCode::LoadLocal, positionSlot);
        Emit(OpCode::LoadLocal, sequenceSlot);
        EmitSequenceCount(stmt);
        Emit(OpCode::LessInt);
        const u32 exitJump = EmitJump(OpCode::JumpIfFalse);

        Emit(OpCode::LoadLocal, sequenceSlot);
        Emit(OpCode::LoadLocal, positionSlot);
        EmitSequenceElement(stmt);
        EmitStoreRun(OpCode::StoreLocal, OpCode::StoreLocalWide, elementSlot, SlotWidth(stmt.declaredType));

        m_loops.emplace_back();
        EmitStmt(*stmt.body);

        // `continue` skips the rest of the body but still advances, or
        // the loop would never end.
        const u32 stepLabel = Here();
        Emit(OpCode::LoadLocal, positionSlot);
        Emit(OpCode::PushInt, IntConstant(1));
        Emit(OpCode::AddInt);
        Emit(OpCode::StoreLocal, positionSlot);
        Emit(OpCode::Jump, conditionLabel);

        const u32 endLabel = Here();
        PatchJump(exitJump, endLabel);
        CloseLoop(stepLabel, endLabel);
    }

    void EmitSequenceCount(const ForEachStmt& stmt)
    {
        if (stmt.overArray)
        {
            Emit(OpCode::ArrayLength);
            return;
        }
        EmitBoundCall(stmt.countTarget, stmt.countFunction, stmt.location);
    }

    void EmitSequenceElement(const ForEachStmt& stmt)
    {
        if (stmt.overArray)
        {
            // A sequence walked this way hands over whole elements, so
            // the width is the loop variable's own.
            Emit(OpCode::LoadElement, SlotWidth(stmt.declaredType));
            return;
        }
        EmitBoundCall(stmt.elementTarget, stmt.elementFunction, stmt.location);
    }

    void EmitBoundCall(CallTarget target, u32 functionIndex, const SourceLocation& location)
    {
        switch (target)
        {
            case CallTarget::ScriptMethod:
            case CallTarget::InstanceMethod:
                Emit(OpCode::Call, functionIndex);
                return;
            case CallTarget::VirtualMethod:
                Emit(OpCode::CallVirtual, functionIndex);
                return;
            case CallTarget::InterfaceMethod:
                Emit(OpCode::CallInterface, functionIndex);
                return;
            default:
                m_diagnostics.AddError(location, "this call was never bound to a method");
                return;
        }
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
        LineScope line(*this, expr.location.line);

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

            case ExprKind::NullLiteral:
                Emit(OpCode::PushNull);
                break;

            case ExprKind::This:
                EmitLoadRun(OpCode::LoadLocal, OpCode::LoadLocalWide, 0, m_receiverIsValue ? m_receiverSlots : 1u);
                break;

            case ExprKind::New: EmitNew(static_cast<const NewExpr&>(expr)); break;

            case ExprKind::NewArray: {
                const auto& node = static_cast<const NewArrayExpr&>(expr);
                if (node.arrayClass == kNoClass)
                {
                    m_diagnostics.AddError(expr.location, "this allocation was never bound to a sequence type");
                    break;
                }
                EmitExpr(*node.length);
                Emit(OpCode::NewArray, node.arrayClass);
                break;
            }

            case ExprKind::Index:
            case ExprKind::Identifier: {
                const ValuePath path = ResolvePath(expr);
                if (path.kind == ValuePath::Kind::Stack)
                {
                    m_diagnostics.AddError(expr.location, "this reference was never bound to any storage");
                    break;
                }
                EmitLoadPath(path);
                break;
            }

            case ExprKind::Member: {
                const auto& member = static_cast<const MemberExpr&>(expr);
                if (member.binding == MemberBinding::ArrayLength && member.base)
                {
                    EmitExpr(*member.base);
                    Emit(OpCode::ArrayLength);
                    break;
                }
                // A constant of a named set was settled while the source
                // was being read, so what is left is the number itself.
                if (member.binding == MemberBinding::EnumConstant)
                {
                    Emit(OpCode::PushInt, IntConstant((i32)member.constantValue));
                    break;
                }
                if (member.binding != MemberBinding::Field || !member.base)
                {
                    m_diagnostics.AddError(expr.location, "this member reference was never bound to a field");
                    break;
                }
                EmitLoadPath(ResolvePath(expr));
                break;
            }

            case ExprKind::Unary: EmitUnary(static_cast<const UnaryExpr&>(expr)); break;
            case ExprKind::Convert: EmitConvert(static_cast<const ConvertExpr&>(expr)); break;
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

    // Discards what an expression evaluated for its side effects left
    // behind, which is a run of slots when it produced a value type.
    void EmitDiscard(const Expr& expr)
    {
        if (expr.resolvedType == ValueType::Void) return;
        const u32 width = SlotWidth(expr);
        for (u32 i = 0; i < width; ++i) Emit(OpCode::Pop);
    }

    // The new object is its own constructor's receiver and the value the
    // expression produces, so it is duplicated once and the copy is what
    // the constructor consumes.
    //
    // A value type has no object to duplicate: the constructor is handed
    // a zeroed value of the right width in the slots a receiver would
    // occupy, and hands back what it made of it.
    void EmitNew(const NewExpr& expr)
    {
        if (expr.classIndex == kNoClass || expr.constructorFunction == kNoFunction)
        {
            m_diagnostics.AddError(expr.location, "this allocation was never bound to a class");
            return;
        }

        if (expr.resolvedType == ValueType::Struct)
        {
            Emit(OpCode::PushZero, SlotWidth(expr));
            for (const ExprPtr& arg : expr.args) EmitExpr(*arg);
            Emit(OpCode::Call, expr.constructorFunction);
            return;
        }

        Emit(OpCode::NewObject, expr.classIndex);
        Emit(OpCode::Dup);
        for (const ExprPtr& arg : expr.args) EmitExpr(*arg);
        Emit(OpCode::Call, expr.constructorFunction);
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

    void EmitConvert(const ConvertExpr& expr)
    {
        EmitExpr(*expr.operand);

        // Naming the type a value already has is allowed and costs
        // nothing, which is worth keeping cheap: generated code cannot
        // always tell in advance that it is asking for nothing.
        const ValueType from = expr.operand->resolvedType;
        if (from == expr.target) return;

        if (expr.target == ValueType::Float) Emit(OpCode::IntToFloat);
        else Emit(OpCode::FloatToInt);
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

    // An assignment is an expression, so every form below ends by reading
    // the stored value back; a statement-level assignment discards it.
    // What a compound form combines with is always a single slot, since
    // no value type is a number or a piece of text.
    void EmitAssign(const AssignExpr& expr)
    {
        const ValuePath path = ResolvePath(*expr.target);

        switch (path.kind)
        {
            case ValuePath::Kind::Local: EmitLocalAssign(expr, path); return;
            case ValuePath::Kind::Field: EmitFieldAssign(expr, path); return;
            case ValuePath::Kind::Element: EmitElementAssign(expr, path); return;
            default:
                m_diagnostics.AddError(expr.location, "this assignment has no storage to write into");
                return;
        }
    }

    void EmitLocalAssign(const AssignExpr& expr, const ValuePath& path)
    {
        if (expr.op == AssignOp::Assign)
        {
            EmitExpr(*expr.value);
        }
        else
        {
            Emit(OpCode::LoadLocal, path.slot);
            EmitExpr(*expr.value);
            Emit(ArithmeticOpcode(BinaryOpForCompound(expr.op), expr.operandType));
        }

        EmitStoreRun(OpCode::StoreLocal, OpCode::StoreLocalWide, path.slot, path.width);
        EmitLoadRun(OpCode::LoadLocal, OpCode::LoadLocalWide, path.slot, path.width);
    }

    void EmitFieldAssign(const AssignExpr& expr, const ValuePath& path)
    {
        if (expr.op == AssignOp::Assign)
        {
            // One copy of the object is consumed by the store, the other
            // reads the field back as the expression's value.
            EmitPathReceiver(path);
            Emit(OpCode::Dup);
            EmitExpr(*expr.value);
            EmitStoreRun(OpCode::StoreField, OpCode::StoreFieldWide, path.slot, path.width);
            EmitLoadRun(OpCode::LoadField, OpCode::LoadFieldWide, path.slot, path.width);
            return;
        }

        // A compound form needs the object a third time, to read the
        // value it is combining with.
        EmitPathReceiver(path);
        Emit(OpCode::Dup);
        Emit(OpCode::Dup);
        Emit(OpCode::LoadField, path.slot);
        EmitExpr(*expr.value);
        Emit(ArithmeticOpcode(BinaryOpForCompound(expr.op), expr.operandType));
        Emit(OpCode::StoreField, path.slot);
        Emit(OpCode::LoadField, path.slot);
    }

    // An element is named by two values rather than one, so a store that
    // also has to produce what it stored keeps both of them: the pair is
    // copied, the store consumes one copy, and the read that follows uses
    // the other.
    void EmitElementAssign(const AssignExpr& expr, const ValuePath& path)
    {
        EmitExpr(*path.array);
        EmitExpr(*path.index);

        if (expr.op == AssignOp::Assign)
        {
            Emit(OpCode::Dup2);
            EmitExpr(*expr.value);
            EmitElementAccess(*path.array, path, true);
            EmitElementAccess(*path.array, path, false);
            return;
        }

        // A compound form needs the pair a third time, to read the value
        // it is combining with.
        Emit(OpCode::Dup2);
        Emit(OpCode::Dup2);
        EmitElementAccess(*path.array, path, false);
        EmitExpr(*expr.value);
        Emit(ArithmeticOpcode(BinaryOpForCompound(expr.op), expr.operandType));
        EmitElementAccess(*path.array, path, true);
        EmitElementAccess(*path.array, path, false);
    }

    // Writes down what a call into the engine names, so the module says
    // which type and which method it wants in text and the machine that
    // runs it decides whether its own table has them.
    u32 RegisterBoundCall(const CallExpr& expr)
    {
        const BoundType& type = m_bindings->types[expr.boundType];
        const BoundMethod& method = type.methods[expr.boundMethod];

        BoundCallSite site;
        site.typeName = type.name;
        site.methodName = method.name;
        site.isInstance = method.isInstance;
        site.parameterTypes = method.parameterTypes;
        site.returnType = method.returnType;

        const u32 index = (u32)m_module.boundCalls.size();
        m_module.boundCalls.push_back(std::move(site));
        return index;
    }

    void EmitCall(const CallExpr& expr)
    {
        if (expr.target == CallTarget::Native)
        {
            for (const ExprPtr& arg : expr.args) EmitExpr(*arg);
            Emit(OpCode::CallNative, expr.targetIndex);
            return;
        }

        if (expr.target == CallTarget::BoundMethod)
        {
            if (!m_bindings || expr.boundType >= m_bindings->types.size() ||
                expr.boundMethod >= m_bindings->types[expr.boundType].methods.size())
            {
                m_diagnostics.AddError(expr.location, "this call was never bound to an engine method");
                return;
            }

            if (expr.receiver) EmitExpr(*expr.receiver);
            for (const ExprPtr& arg : expr.args) EmitExpr(*arg);
            Emit(OpCode::CallBound, RegisterBoundCall(expr));
            return;
        }

        // The receiver becomes the callee's slot zero, so it goes on the
        // stack ahead of everything the callee was written to take.
        if (expr.receiver) EmitExpr(*expr.receiver);
        for (const ExprPtr& arg : expr.args) EmitExpr(*arg);

        switch (expr.target)
        {
            case CallTarget::ScriptMethod:
            case CallTarget::InstanceMethod:
                Emit(OpCode::Call, expr.targetIndex);
                return;
            case CallTarget::VirtualMethod:
                Emit(OpCode::CallVirtual, expr.targetIndex);
                return;
            case CallTarget::InterfaceMethod:
                Emit(OpCode::CallInterface, expr.targetIndex);
                return;
            default:
                m_diagnostics.AddError(expr.location, "this call was never bound to a method");
                return;
        }
    }
};

} // namespace

BytecodeModule Emit(const Program& program, const std::string& sourceName, u32 moduleVersion, DiagnosticList& diagnostics,
    const BindingTable* bindings)
{
    EmitterState emitter(program, sourceName, moduleVersion, diagnostics, bindings);
    return emitter.Run();
}

} // namespace Fluxion::Script
