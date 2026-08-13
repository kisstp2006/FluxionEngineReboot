#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fluxion::Script
{

namespace
{

bool IsNumeric(ValueType type) { return type == ValueType::Int || type == ValueType::Float; }

// True once an error has already been reported for the expression that
// produced this type, so further complaints about it would only be noise.
bool IsPoisoned(ValueType type) { return type == ValueType::Unknown; }

bool IsReference(ValueType type) { return type == ValueType::Object || type == ValueType::Null; }

class Analyzer
{
public:
    Analyzer(Program& program, DiagnosticList& diagnostics)
        : m_program(program), m_diagnostics(diagnostics)
    {
    }

    bool Run()
    {
        const NativeFunctionSignature* natives = NativeFunctionTable();
        for (u32 i = 0; i < kNativeFunctionCount; ++i)
            m_natives[natives[i].qualifiedName].push_back((NativeFunctionId)i);

        CollectClasses();
        ResolveBaseLists();
        CheckClassShapes();
        if (m_diagnostics.HasErrors()) return false;

        SynthesizeConstructors();
        AssignFunctionIndices();
        ResolveMemberTypes();
        if (m_diagnostics.HasErrors()) return false;

        for (u32 i = 0; i < (u32)m_classes.size(); ++i) BuildLayout(i);
        if (m_diagnostics.HasErrors()) return false;

        for (ClassEntry& entry : m_classes)
        {
            m_currentClass = &entry;
            for (DeclPtr& method : entry.decl->methods)
                AnalyzeMethod(*static_cast<MethodDecl*>(method.get()));
            m_currentClass = nullptr;
        }

        return !m_diagnostics.HasErrors();
    }

private:
    struct FieldEntry
    {
        std::string name;
        TypeRef type;
        u32 slot = 0;
    };

    struct ClassEntry
    {
        ClassDecl* decl = nullptr;
        u32 index = kNoClass;

        u32 base = kNoClass;
        std::vector<u32> declaredInterfaces;

        // Every interface this class answers to, its base's included, so
        // one lookup settles whether a reference can be seen as an
        // interface.
        std::vector<u32> allInterfaces;

        // 0 = not started, 1 = being laid out, 2 = finished. The middle
        // state is what catches a class that ends up inheriting from
        // itself.
        int layoutState = 0;

        // Fields of the whole chain, the base's first, so a slot means
        // the same thing whichever class a reference is seen as.
        std::vector<FieldEntry> fields;

        std::vector<u32> vtable;
        std::vector<MethodDecl*> vtableMethods;

        MethodDecl* constructor = nullptr;
    };

    struct LocalSymbol
    {
        TypeRef type;
        int slot = -1;
    };

    // What a frame slot has been used for. A slot is never shared between
    // a reference and a plain value, so the bitmap that drives collection
    // stays exact even though sibling scopes reuse slots.
    enum class SlotUse : u8 { Free, Value, Reference };

    Program& m_program;
    DiagnosticList& m_diagnostics;

    std::vector<ClassEntry> m_classes;
    std::unordered_map<std::string, u32> m_classByName;
    std::unordered_map<std::string, MethodDecl*> m_methods; // keyed "Class.Method"
    std::unordered_map<std::string, std::vector<NativeFunctionId>> m_natives;

    ClassEntry* m_currentClass = nullptr;
    MethodDecl* m_currentMethod = nullptr;
    bool m_currentMethodIsInstance = false;

    std::vector<std::unordered_map<std::string, LocalSymbol>> m_scopes;
    std::vector<int> m_scopeSlotMarks;
    std::vector<SlotUse> m_slotUses;
    int m_nextSlot = 0;
    int m_highWaterSlot = 0;
    int m_loopDepth = 0;

    void Error(const SourceLocation& location, std::string message)
    {
        m_diagnostics.AddError(location, std::move(message));
    }

    // --- Naming types in messages -------------------------------------------

    std::string Quoted(ValueType type, u32 classIndex) const
    {
        if (type == ValueType::Object)
        {
            if (classIndex < m_classes.size()) return "'" + m_classes[classIndex].decl->name + "'";
            return "a reference";
        }
        return std::string("'") + ValueTypeName(type) + "'";
    }

    std::string Quoted(const TypeRef& type) const { return Quoted(type.type, type.classIndex); }
    std::string Quoted(const Expr& expr) const { return Quoted(expr.resolvedType, expr.resolvedClass); }

    const std::string& ClassName(u32 index) const
    {
        static const std::string unknown = "<unknown>";
        return index < m_classes.size() ? m_classes[index].decl->name : unknown;
    }

    // --- Declaration collection ---------------------------------------------

    void CollectClasses()
    {
        for (DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Class) continue;
            auto* classDecl = static_cast<ClassDecl*>(decl.get());

            ClassEntry entry;
            entry.decl = classDecl;
            entry.index = (u32)m_classes.size();
            classDecl->classIndex = entry.index;

            if (!m_classByName.emplace(classDecl->name, entry.index).second)
                Error(classDecl->location, "'" + classDecl->name + "' is declared more than once");

            m_classes.push_back(std::move(entry));
        }
    }

    bool FindClass(const std::string& name, u32& outIndex) const
    {
        auto found = m_classByName.find(name);
        if (found == m_classByName.end()) return false;
        outIndex = found->second;
        return true;
    }

    void ResolveBaseLists()
    {
        for (ClassEntry& entry : m_classes)
        {
            ClassDecl& decl = *entry.decl;

            if (decl.isInterface && !decl.baseNames.empty())
            {
                Error(decl.baseLocations[0], "interface '" + decl.name + "' cannot declare a base list");
                continue;
            }
            if (decl.isStatic && !decl.baseNames.empty())
            {
                Error(decl.baseLocations[0], "'" + decl.name + "' holds only static members, so it cannot declare a base list");
                continue;
            }

            for (size_t i = 0; i < decl.baseNames.size(); ++i)
            {
                const std::string& name = decl.baseNames[i];
                const SourceLocation& location = decl.baseLocations[i];

                u32 target = kNoClass;
                if (!FindClass(name, target))
                {
                    Error(location, "no type named '" + name + "' is declared");
                    continue;
                }
                if (target == entry.index)
                {
                    Error(location, "'" + decl.name + "' cannot list itself as a base type");
                    continue;
                }

                if (m_classes[target].decl->isInterface)
                {
                    bool duplicate = false;
                    for (u32 existing : entry.declaredInterfaces) duplicate = duplicate || existing == target;
                    if (duplicate)
                        Error(location, "'" + decl.name + "' lists interface '" + name + "' more than once");
                    else
                        entry.declaredInterfaces.push_back(target);
                    continue;
                }

                if (i != 0)
                {
                    Error(location, "base class '" + name + "' must come first in the base list of '" + decl.name + "'");
                    continue;
                }
                if (m_classes[target].decl->isStatic)
                {
                    Error(location, "'" + name + "' holds only static members and cannot be used as a base class");
                    continue;
                }
                entry.base = target;
            }
        }
    }

    void CheckClassShapes()
    {
        for (ClassEntry& entry : m_classes)
        {
            ClassDecl& decl = *entry.decl;

            for (DeclPtr& methodDecl : decl.methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());

                if (decl.isInterface)
                {
                    if (method->isConstructor)
                        Error(method->location, "interface '" + decl.name + "' cannot declare a constructor");
                    else if (method->isStatic)
                        Error(method->location, "'" + decl.name + "." + method->name + "' cannot be 'static': an interface declares instance methods only");
                    else if (method->isVirtual || method->isOverride)
                        Error(method->location, "'" + decl.name + "." + method->name + "' is already dispatched on, so it cannot be declared 'virtual' or 'override'");
                    continue;
                }

                if (decl.isStatic)
                {
                    if (method->isConstructor)
                        Error(method->location, "'" + decl.name + "' holds only static members, so it cannot declare a constructor");
                    else if (!method->isStatic)
                        Error(method->location, "'" + decl.name + "." + method->name + "' must be declared 'static', because '" + decl.name + "' holds only static members");
                    continue;
                }

                if (method->isStatic && (method->isVirtual || method->isOverride))
                    Error(method->location, "'" + decl.name + "." + method->name + "' cannot be both 'static' and dispatched on the instance");
            }

            if (decl.isInterface && !decl.fields.empty())
                Error(decl.fields[0]->location, "interface '" + decl.name + "' cannot declare a field");
            else if (decl.isStatic && !decl.fields.empty())
                Error(decl.fields[0]->location, "'" + decl.name + "' holds only static members, so it cannot declare a field");
        }
    }

    // Every class that can be instantiated ends up with a constructor,
    // so a `new` and a base chain never have to special-case its absence.
    void SynthesizeConstructors()
    {
        for (ClassEntry& entry : m_classes)
        {
            ClassDecl& decl = *entry.decl;
            if (decl.isInterface || decl.isStatic) continue;

            for (DeclPtr& methodDecl : decl.methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());
                if (!method->isConstructor) continue;
                if (entry.constructor)
                {
                    Error(method->location, "'" + decl.name + "' declares more than one constructor");
                    continue;
                }
                entry.constructor = method;
            }
            if (entry.constructor) continue;

            auto synthesized = std::make_unique<MethodDecl>();
            synthesized->location = decl.location;
            synthesized->name = decl.name;
            synthesized->isConstructor = true;
            synthesized->returnType.type = ValueType::Void;
            synthesized->body = std::make_unique<BlockStmt>();
            synthesized->body->location = decl.location;

            entry.constructor = synthesized.get();
            decl.methods.push_back(std::move(synthesized));
        }
    }

    // Every method is numbered here, before any body is looked at, so a
    // call can be resolved against a method declared later in the file.
    void AssignFunctionIndices()
    {
        u32 nextFunctionIndex = 0;
        for (ClassEntry& entry : m_classes)
        {
            ClassDecl& decl = *entry.decl;
            for (DeclPtr& methodDecl : decl.methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());
                method->owningClass = entry.index;
                method->qualifiedName = decl.name + "." + method->name;
                method->functionIndex = nextFunctionIndex++;

                if (method->isConstructor) continue;
                if (!m_methods.emplace(method->qualifiedName, method).second)
                    Error(method->location, "method '" + method->qualifiedName + "' is declared more than once");
            }
            if (entry.constructor) decl.constructorFunction = entry.constructor->functionIndex;
        }
    }

    bool ResolveTypeRef(TypeRef& type, const SourceLocation& location, const std::string& what)
    {
        if (type.type != ValueType::Object) return true;

        u32 index = kNoClass;
        if (!FindClass(type.name, index))
        {
            Error(location, "no type named '" + type.name + "' is declared, " + what);
            type.type = ValueType::Unknown;
            return false;
        }
        if (m_classes[index].decl->isStatic)
        {
            Error(location, "'" + type.name + "' holds only static members, so nothing can have it as a type");
            type.type = ValueType::Unknown;
            return false;
        }
        type.classIndex = index;
        return true;
    }

    void ResolveMemberTypes()
    {
        for (ClassEntry& entry : m_classes)
        {
            ClassDecl& decl = *entry.decl;

            for (DeclPtr& fieldDecl : decl.fields)
            {
                auto* field = static_cast<FieldDecl*>(fieldDecl.get());
                if (field->type.type == ValueType::Void)
                {
                    Error(field->location, "field '" + field->name + "' cannot have type 'void'");
                    field->type.type = ValueType::Unknown;
                    continue;
                }
                ResolveTypeRef(field->type, field->location, "so field '" + field->name + "' has no type");
            }

            for (DeclPtr& methodDecl : decl.methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());
                ResolveTypeRef(method->returnType, method->location,
                    "so '" + method->qualifiedName + "' has no return type");

                for (ParamDecl& param : method->params)
                {
                    if (param.type.type == ValueType::Void)
                    {
                        Error(param.location, "parameter '" + param.name + "' cannot have type 'void'");
                        param.type.type = ValueType::Unknown;
                        continue;
                    }
                    ResolveTypeRef(param.type, param.location, "so parameter '" + param.name + "' has no type");
                }
            }
        }
    }

    // --- Layout --------------------------------------------------------------

    void BuildLayout(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        if (entry.layoutState == 2) return;
        if (entry.layoutState == 1)
        {
            Error(entry.decl->location, "'" + entry.decl->name + "' inherits from itself through its base list");
            entry.layoutState = 2;
            entry.base = kNoClass;
            return;
        }
        entry.layoutState = 1;

        // Everything this class builds on has to be finished first: its
        // fields sit after the base's, and its table entries replace the
        // base's in place.
        if (entry.base != kNoClass) BuildLayout(entry.base);
        for (u32 iface : entry.declaredInterfaces) BuildLayout(iface);

        BuildFields(index);
        BuildVirtualTable(index);
        BuildInterfaceTables(index);

        m_classes[index].layoutState = 2;
        Publish(index);
    }

    void BuildFields(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        ClassDecl& decl = *entry.decl;

        if (entry.base != kNoClass) entry.fields = m_classes[entry.base].fields;

        for (DeclPtr& fieldDecl : decl.fields)
        {
            auto* field = static_cast<FieldDecl*>(fieldDecl.get());

            bool duplicate = false;
            for (const FieldEntry& existing : entry.fields) duplicate = duplicate || existing.name == field->name;
            if (duplicate)
            {
                Error(field->location, "field '" + field->name + "' is already declared in '" + decl.name + "' or one of its base classes");
                continue;
            }

            FieldEntry created;
            created.name = field->name;
            created.type = field->type;
            created.slot = (u32)entry.fields.size();
            field->fieldSlot = created.slot;
            entry.fields.push_back(std::move(created));
        }
    }

    void BuildVirtualTable(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        ClassDecl& decl = *entry.decl;

        if (entry.base != kNoClass)
        {
            entry.vtable = m_classes[entry.base].vtable;
            entry.vtableMethods = m_classes[entry.base].vtableMethods;
        }

        for (DeclPtr& methodDecl : decl.methods)
        {
            auto* method = static_cast<MethodDecl*>(methodDecl.get());
            if (method->isConstructor || method->isStatic) continue;

            // An interface numbers each of its own methods, which is what
            // a call through it dispatches on.
            if (decl.isInterface)
            {
                method->vtableSlot = (u32)entry.vtable.size();
                entry.vtable.push_back(method->functionIndex);
                entry.vtableMethods.push_back(method);
                continue;
            }

            MethodDecl* inherited = FindMethodInChain(entry.base, method->name);

            if (method->isOverride)
            {
                if (!inherited)
                {
                    Error(method->location, "'" + method->qualifiedName + "' is declared 'override', but no base class declares a method named '" + method->name + "'");
                    continue;
                }
                if (inherited->vtableSlot == kNoVtableSlot)
                {
                    Error(method->location, "'" + method->qualifiedName + "' cannot override '" + inherited->qualifiedName + "', which is not declared 'virtual'");
                    continue;
                }
                if (!SameSignature(*method, *inherited))
                {
                    Error(method->location, "'" + method->qualifiedName + "' does not have the same signature as '" + inherited->qualifiedName + "', which it overrides");
                    continue;
                }
                method->vtableSlot = inherited->vtableSlot;
                entry.vtable[inherited->vtableSlot] = method->functionIndex;
                entry.vtableMethods[inherited->vtableSlot] = method;
                continue;
            }

            if (inherited)
            {
                Error(method->location, "'" + method->qualifiedName + "' hides '" + inherited->qualifiedName + "'; declare it 'override' to replace it instead");
                continue;
            }

            if (method->isVirtual)
            {
                method->vtableSlot = (u32)entry.vtable.size();
                entry.vtable.push_back(method->functionIndex);
                entry.vtableMethods.push_back(method);
            }
        }
    }

    void BuildInterfaceTables(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        ClassDecl& decl = *entry.decl;

        if (entry.base != kNoClass) entry.allInterfaces = m_classes[entry.base].allInterfaces;
        for (u32 iface : entry.declaredInterfaces)
        {
            bool present = false;
            for (u32 existing : entry.allInterfaces) present = present || existing == iface;
            if (!present) entry.allInterfaces.push_back(iface);
        }

        if (decl.isInterface || decl.isStatic) return;

        decl.interfaceTables.clear();
        for (u32 iface : entry.allInterfaces)
        {
            InterfaceImplementation implementation;
            implementation.interfaceClass = iface;

            const std::vector<MethodDecl*> wanted = m_classes[iface].vtableMethods;
            for (MethodDecl* want : wanted)
            {
                MethodDecl* have = FindMethodInChain(index, want->name);
                if (!have || have->isStatic || have->isConstructor || !SameSignature(*have, *want))
                {
                    Error(decl.location, "'" + decl.name + "' does not implement '" + want->qualifiedName + "'");
                    implementation.methods.push_back(kNoFunction);
                    continue;
                }

                // A method that is dispatched on may already have been
                // replaced further down the chain, so the vtable is the
                // authority on which body actually runs.
                const u32 target = (have->vtableSlot != kNoVtableSlot)
                    ? m_classes[index].vtable[have->vtableSlot]
                    : have->functionIndex;
                implementation.methods.push_back(target);
            }

            m_classes[index].decl->interfaceTables.push_back(std::move(implementation));
        }
    }

    void Publish(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        ClassDecl& decl = *entry.decl;

        decl.baseClass = entry.base;
        decl.fieldSlotCount = (u32)entry.fields.size();
        decl.vtable = entry.vtable;

        decl.fieldReferenceBits.clear();
        for (const FieldEntry& field : entry.fields)
        {
            if (field.type.type == ValueType::Object) SetReferenceBit(decl.fieldReferenceBits, field.slot);
        }
    }

    MethodDecl* FindMethodInChain(u32 index, const std::string& name) const
    {
        u32 current = index;
        while (current != kNoClass && current < m_classes.size())
        {
            for (const DeclPtr& methodDecl : m_classes[current].decl->methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());
                if (!method->isConstructor && method->name == name) return method;
            }
            current = m_classes[current].base;
        }
        return nullptr;
    }

    const FieldEntry* FindField(u32 index, const std::string& name) const
    {
        if (index >= m_classes.size()) return nullptr;
        for (const FieldEntry& field : m_classes[index].fields)
        {
            if (field.name == name) return &field;
        }
        return nullptr;
    }

    static bool SameType(const TypeRef& a, const TypeRef& b)
    {
        return a.type == b.type && (a.type != ValueType::Object || a.classIndex == b.classIndex);
    }

    static bool SameSignature(const MethodDecl& a, const MethodDecl& b)
    {
        if (!SameType(a.returnType, b.returnType)) return false;
        if (a.params.size() != b.params.size()) return false;
        for (size_t i = 0; i < a.params.size(); ++i)
        {
            if (!SameType(a.params[i].type, b.params[i].type)) return false;
        }
        return true;
    }

    bool IsDerivedFrom(u32 derived, u32 base) const
    {
        u32 current = derived;
        while (current != kNoClass && current < m_classes.size())
        {
            if (current == base) return true;
            current = m_classes[current].base;
        }
        return false;
    }

    bool ImplementsInterface(u32 index, u32 iface) const
    {
        if (index >= m_classes.size()) return false;
        for (u32 existing : m_classes[index].allInterfaces)
        {
            if (existing == iface) return true;
        }
        return false;
    }

    // A reference travels only towards a less specific type: to a base
    // class, or to an interface the class implements. Nothing brings it
    // back the other way, so no check can fail at run time.
    bool IsReferenceAssignable(const Expr& value, const TypeRef& target) const
    {
        if (value.resolvedType == ValueType::Null) return true;
        if (value.resolvedType != ValueType::Object) return false;
        if (value.resolvedClass == target.classIndex) return true;
        if (target.classIndex >= m_classes.size()) return false;
        if (m_classes[target.classIndex].decl->isInterface)
            return ImplementsInterface(value.resolvedClass, target.classIndex);
        return IsDerivedFrom(value.resolvedClass, target.classIndex);
    }

    // --- Methods -------------------------------------------------------------

    void AnalyzeMethod(MethodDecl& method)
    {
        m_currentMethod = &method;
        m_currentMethodIsInstance = !method.isStatic && !m_currentClass->decl->isStatic;

        m_scopes.clear();
        m_scopeSlotMarks.clear();
        m_slotUses.clear();
        m_nextSlot = 0;
        m_highWaterSlot = 0;
        m_loopDepth = 0;

        PushScope();

        if (m_currentMethodIsInstance)
        {
            TypeRef self;
            self.type = ValueType::Object;
            self.classIndex = m_currentClass->index;
            DeclareLocal("this", self, method.location);
        }

        for (ParamDecl& param : method.params)
            DeclareLocal(param.name, param.type, param.location);

        if (method.isConstructor) AnalyzeBaseCall(method);

        if (method.body) AnalyzeStmt(*method.body);
        PopScope();

        method.localSlotCount = (u32)m_highWaterSlot;
        method.localIsReference.assign((size_t)m_highWaterSlot, 0);
        for (size_t i = 0; i < m_slotUses.size() && i < method.localIsReference.size(); ++i)
            method.localIsReference[i] = (m_slotUses[i] == SlotUse::Reference) ? 1u : 0u;

        m_currentMethod = nullptr;
    }

    void AnalyzeBaseCall(MethodDecl& method)
    {
        const u32 base = m_currentClass->base;

        if (base == kNoClass)
        {
            if (method.hasBaseCall)
                Error(method.baseCallLocation, "'" + m_currentClass->decl->name + "' has no base class, so its constructor cannot chain to one");
            return;
        }

        MethodDecl* baseConstructor = m_classes[base].constructor;
        if (!baseConstructor) return;

        method.baseConstructorFunction = baseConstructor->functionIndex;

        if (!method.hasBaseCall)
        {
            if (!baseConstructor->params.empty())
                Error(method.location, "the constructor of '" + m_currentClass->decl->name + "' must begin with ': base(...)', because the constructor of '" +
                                           ClassName(base) + "' takes " + std::to_string(baseConstructor->params.size()) + " argument(s)");
            return;
        }

        for (ExprPtr& arg : method.baseArgs) AnalyzeExpr(*arg);

        if (method.baseArgs.size() != baseConstructor->params.size())
        {
            Error(method.baseCallLocation, "the constructor of '" + ClassName(base) + "' takes " +
                                               std::to_string(baseConstructor->params.size()) + " argument(s) but " +
                                               std::to_string(method.baseArgs.size()) + " were given");
            return;
        }

        for (size_t i = 0; i < method.baseArgs.size(); ++i)
            CheckAssignable(*method.baseArgs[i], baseConstructor->params[i].type,
                "in argument " + std::to_string(i + 1) + " of the base constructor");
    }

    // --- Scopes ------------------------------------------------------------

    void PushScope()
    {
        m_scopes.emplace_back();
        m_scopeSlotMarks.push_back(m_nextSlot);
    }

    void PopScope()
    {
        m_scopes.pop_back();
        // Slots freed by leaving a scope are handed back to the next
        // sibling scope; the high-water mark is what sizes the frame.
        m_nextSlot = m_scopeSlotMarks.back();
        m_scopeSlotMarks.pop_back();
    }

    int DeclareLocal(const std::string& name, const TypeRef& type, const SourceLocation& location)
    {
        auto& scope = m_scopes.back();
        if (scope.find(name) != scope.end())
        {
            Error(location, "'" + name + "' is already declared in this scope");
            return scope[name].slot;
        }

        const SlotUse use = (type.type == ValueType::Object) ? SlotUse::Reference : SlotUse::Value;

        // A slot handed back by an earlier scope is reused only for the
        // same kind of value. Mixing the two would leave the frame's
        // reference bitmap describing whichever declaration came last,
        // and a collection reads that bitmap as fact.
        while (m_nextSlot < (int)m_slotUses.size() && m_slotUses[(size_t)m_nextSlot] != SlotUse::Free &&
               m_slotUses[(size_t)m_nextSlot] != use)
        {
            ++m_nextSlot;
        }

        const int slot = m_nextSlot++;
        if ((int)m_slotUses.size() <= slot) m_slotUses.resize((size_t)slot + 1, SlotUse::Free);
        m_slotUses[(size_t)slot] = use;
        if (m_nextSlot > m_highWaterSlot) m_highWaterSlot = m_nextSlot;

        scope.emplace(name, LocalSymbol{ type, slot });
        return slot;
    }

    bool Lookup(const std::string& name, LocalSymbol& outSymbol) const
    {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope)
        {
            auto found = scope->find(name);
            if (found != scope->end()) { outSymbol = found->second; return true; }
        }
        return false;
    }

    // --- Statements ---------------------------------------------------------

    void AnalyzeStmt(Stmt& stmt)
    {
        switch (stmt.kind)
        {
            case StmtKind::LocalDecl: AnalyzeLocalDecl(static_cast<LocalDeclStmt&>(stmt)); break;

            case StmtKind::Expr:
                AnalyzeExpr(*static_cast<ExprStmt&>(stmt).expr);
                break;

            case StmtKind::Block: {
                auto& block = static_cast<BlockStmt&>(stmt);
                PushScope();
                for (StmtPtr& child : block.statements) AnalyzeStmt(*child);
                PopScope();
                break;
            }

            case StmtKind::If: {
                auto& node = static_cast<IfStmt&>(stmt);
                AnalyzeExpr(*node.condition);
                RequireBool(*node.condition, "if");
                AnalyzeStmt(*node.thenBranch);
                if (node.elseBranch) AnalyzeStmt(*node.elseBranch);
                break;
            }

            case StmtKind::While: {
                auto& node = static_cast<WhileStmt&>(stmt);
                AnalyzeExpr(*node.condition);
                RequireBool(*node.condition, "while");
                ++m_loopDepth;
                AnalyzeStmt(*node.body);
                --m_loopDepth;
                break;
            }

            case StmtKind::For: {
                auto& node = static_cast<ForStmt&>(stmt);
                PushScope();
                if (node.init) AnalyzeStmt(*node.init);
                if (node.condition)
                {
                    AnalyzeExpr(*node.condition);
                    RequireBool(*node.condition, "for");
                }
                if (node.step) AnalyzeExpr(*node.step);
                ++m_loopDepth;
                AnalyzeStmt(*node.body);
                --m_loopDepth;
                PopScope();
                break;
            }

            case StmtKind::Return: AnalyzeReturn(static_cast<ReturnStmt&>(stmt)); break;

            case StmtKind::Break:
                if (m_loopDepth == 0) Error(stmt.location, "'break' is only allowed inside a loop");
                break;

            case StmtKind::Continue:
                if (m_loopDepth == 0) Error(stmt.location, "'continue' is only allowed inside a loop");
                break;

            default: break;
        }
    }

    void AnalyzeLocalDecl(LocalDeclStmt& stmt)
    {
        if (stmt.initializer) AnalyzeExpr(*stmt.initializer);

        if (stmt.inferred)
        {
            if (!stmt.initializer)
            {
                Error(stmt.location, "'" + stmt.name + "' needs an initializer for its type to be inferred");
                stmt.declaredType.type = ValueType::Unknown;
            }
            else if (stmt.initializer->resolvedType == ValueType::Void)
            {
                Error(stmt.initializer->location, "cannot infer the type of '" + stmt.name + "' from an expression that produces no value");
                stmt.declaredType.type = ValueType::Unknown;
            }
            else if (stmt.initializer->resolvedType == ValueType::Null)
            {
                Error(stmt.initializer->location, "cannot infer the type of '" + stmt.name + "' from 'null'");
                stmt.declaredType.type = ValueType::Unknown;
            }
            else
            {
                stmt.declaredType.type = stmt.initializer->resolvedType;
                stmt.declaredType.classIndex = stmt.initializer->resolvedClass;
            }
        }
        else
        {
            if (stmt.declaredType.type == ValueType::Void)
            {
                Error(stmt.location, "variable '" + stmt.name + "' cannot have type 'void'");
                stmt.declaredType.type = ValueType::Unknown;
            }
            else
            {
                ResolveTypeRef(stmt.declaredType, stmt.location, "so variable '" + stmt.name + "' has no type");
            }
            if (stmt.initializer)
                CheckAssignable(*stmt.initializer, stmt.declaredType, "in the initializer of '" + stmt.name + "'");
        }

        stmt.localSlot = DeclareLocal(stmt.name, stmt.declaredType, stmt.location);
    }

    void AnalyzeReturn(ReturnStmt& stmt)
    {
        TypeRef returnType;
        returnType.type = ValueType::Void;
        if (m_currentMethod) returnType = m_currentMethod->returnType;

        if (!stmt.value)
        {
            if (returnType.type != ValueType::Void)
                Error(stmt.location, "a method returning " + Quoted(returnType) + " must return a value");
            return;
        }

        AnalyzeExpr(*stmt.value);
        if (returnType.type == ValueType::Void)
        {
            Error(stmt.location, "a method returning 'void' cannot return a value");
            return;
        }
        CheckAssignable(*stmt.value, returnType, "in a return statement");
    }

    void RequireBool(Expr& expr, const char* construct)
    {
        if (expr.resolvedType == ValueType::Bool || IsPoisoned(expr.resolvedType)) return;
        Error(expr.location, std::string("the condition of '") + construct + "' must be 'bool', found " + Quoted(expr));
    }

    // The conversions a value may go through on its way into a declared
    // type: an int widening to a float, and a reference being seen as
    // something less specific.
    bool CheckAssignable(Expr& value, const TypeRef& target, const std::string& context)
    {
        if (IsPoisoned(target.type) || IsPoisoned(value.resolvedType)) return true;

        if (IsReference(target.type) || IsReference(value.resolvedType))
        {
            if (target.type == ValueType::Object && IsReferenceAssignable(value, target)) return true;
            Error(value.location, "cannot convert " + Quoted(value) + " to " + Quoted(target) + " " + context);
            return false;
        }

        if (value.resolvedType == target.type) return true;
        if (target.type == ValueType::Float && value.resolvedType == ValueType::Int)
        {
            value.conversion = ValueType::Float;
            return true;
        }
        Error(value.location, "cannot convert " + Quoted(value) + " to " + Quoted(target) + " " + context);
        return false;
    }

    // --- Expressions ---------------------------------------------------------

    void AnalyzeExpr(Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral: expr.resolvedType = ValueType::Int; break;
            case ExprKind::FloatLiteral: expr.resolvedType = ValueType::Float; break;
            case ExprKind::BoolLiteral: expr.resolvedType = ValueType::Bool; break;
            case ExprKind::StringLiteral: expr.resolvedType = ValueType::String; break;
            case ExprKind::NullLiteral: expr.resolvedType = ValueType::Null; break;

            case ExprKind::This: {
                if (!m_currentMethodIsInstance)
                {
                    Error(expr.location, "'this' is only available inside an instance method");
                    expr.resolvedType = ValueType::Unknown;
                    break;
                }
                expr.resolvedType = ValueType::Object;
                expr.resolvedClass = m_currentClass->index;
                break;
            }

            case ExprKind::New: AnalyzeNew(static_cast<NewExpr&>(expr)); break;
            case ExprKind::Identifier: AnalyzeIdentifier(static_cast<IdentifierExpr&>(expr)); break;
            case ExprKind::Member: AnalyzeMemberValue(static_cast<MemberExpr&>(expr)); break;
            case ExprKind::Call: AnalyzeCall(static_cast<CallExpr&>(expr)); break;
            case ExprKind::Unary: AnalyzeUnary(static_cast<UnaryExpr&>(expr)); break;
            case ExprKind::Binary: AnalyzeBinary(static_cast<BinaryExpr&>(expr)); break;
            case ExprKind::Assign: AnalyzeAssign(static_cast<AssignExpr&>(expr)); break;

            default: expr.resolvedType = ValueType::Unknown; break;
        }
    }

    void AnalyzeNew(NewExpr& expr)
    {
        expr.resolvedType = ValueType::Unknown;

        u32 index = kNoClass;
        if (!FindClass(expr.typeName, index))
        {
            Error(expr.location, "no type named '" + expr.typeName + "' is declared");
            return;
        }

        ClassDecl& decl = *m_classes[index].decl;
        if (decl.isInterface)
        {
            Error(expr.location, "interface '" + expr.typeName + "' cannot be created with 'new', only a class implementing it can");
            return;
        }
        if (decl.isStatic)
        {
            Error(expr.location, "'" + expr.typeName + "' holds only static members and cannot be created with 'new'");
            return;
        }

        MethodDecl* constructor = m_classes[index].constructor;
        if (!constructor) return;

        expr.classIndex = index;
        expr.constructorFunction = constructor->functionIndex;
        expr.resolvedType = ValueType::Object;
        expr.resolvedClass = index;

        for (ExprPtr& arg : expr.args) AnalyzeExpr(*arg);

        if (expr.args.size() != constructor->params.size())
        {
            Error(expr.location, "the constructor of '" + expr.typeName + "' takes " + std::to_string(constructor->params.size()) +
                                     " argument(s) but " + std::to_string(expr.args.size()) + " were given");
            return;
        }
        for (size_t i = 0; i < expr.args.size(); ++i)
            CheckAssignable(*expr.args[i], constructor->params[i].type,
                "in argument " + std::to_string(i + 1) + " of the constructor of '" + expr.typeName + "'");
    }

    void AnalyzeIdentifier(IdentifierExpr& expr)
    {
        LocalSymbol symbol;
        if (Lookup(expr.name, symbol))
        {
            expr.localSlot = symbol.slot;
            expr.resolvedType = symbol.type.type;
            expr.resolvedClass = symbol.type.classIndex;
            return;
        }

        const FieldEntry* field = m_currentClass ? FindField(m_currentClass->index, expr.name) : nullptr;
        if (field)
        {
            if (!m_currentMethodIsInstance)
            {
                Error(expr.location, "field '" + expr.name + "' belongs to an instance and cannot be reached from a static method");
                expr.resolvedType = ValueType::Unknown;
                return;
            }
            expr.isField = true;
            expr.fieldSlot = field->slot;
            expr.resolvedType = field->type.type;
            expr.resolvedClass = field->type.classIndex;
            return;
        }

        Error(expr.location, "use of undeclared identifier '" + expr.name + "'");
        expr.resolvedType = ValueType::Unknown;
    }

    // Whether `expr` is a name that stands for a type rather than a
    // value, which is how a static call is written.
    bool NamesAClass(const Expr& expr, u32& outIndex) const
    {
        if (expr.kind != ExprKind::Identifier) return false;
        const auto& identifier = static_cast<const IdentifierExpr&>(expr);

        LocalSymbol symbol;
        if (Lookup(identifier.name, symbol)) return false;
        if (m_currentClass && FindField(m_currentClass->index, identifier.name)) return false;
        return FindClass(identifier.name, outIndex);
    }

    void AnalyzeMemberValue(MemberExpr& expr)
    {
        u32 classIndex = kNoClass;
        if (NamesAClass(*expr.base, classIndex))
        {
            Error(expr.location, "'" + ClassName(classIndex) + "." + expr.member + "' is not a value; only a method can be named this way");
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        AnalyzeExpr(*expr.base);
        if (IsPoisoned(expr.base->resolvedType))
        {
            expr.resolvedType = ValueType::Unknown;
            return;
        }
        if (expr.base->resolvedType != ValueType::Object)
        {
            Error(expr.location, Quoted(*expr.base) + " has no member named '" + expr.member + "'");
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        const FieldEntry* field = FindField(expr.base->resolvedClass, expr.member);
        if (!field)
        {
            Error(expr.location, "'" + ClassName(expr.base->resolvedClass) + "' has no field named '" + expr.member + "'");
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        expr.binding = MemberBinding::Field;
        expr.fieldSlot = field->slot;
        expr.resolvedType = field->type.type;
        expr.resolvedClass = field->type.classIndex;
    }

    void AnalyzeUnary(UnaryExpr& expr)
    {
        AnalyzeExpr(*expr.operand);
        const ValueType operandType = expr.operand->resolvedType;

        if (expr.op == UnaryOp::Not)
        {
            if (operandType != ValueType::Bool && !IsPoisoned(operandType))
                Error(expr.location, "'!' needs a 'bool' operand, found " + Quoted(*expr.operand));
            expr.resolvedType = ValueType::Bool;
            return;
        }

        if (!IsNumeric(operandType))
        {
            if (!IsPoisoned(operandType))
                Error(expr.location, "unary '-' needs an 'int' or a 'float' operand, found " + Quoted(*expr.operand));
            expr.resolvedType = ValueType::Unknown;
            return;
        }
        expr.resolvedType = operandType;
    }

    // Brings a numeric pair to a common type, widening the int side when
    // the other side is a float. Returns Unknown when the pair is not
    // numeric at all.
    ValueType UnifyNumeric(Expr& lhs, Expr& rhs)
    {
        if (!IsNumeric(lhs.resolvedType) || !IsNumeric(rhs.resolvedType)) return ValueType::Unknown;
        if (lhs.resolvedType == ValueType::Float || rhs.resolvedType == ValueType::Float)
        {
            if (lhs.resolvedType == ValueType::Int) lhs.conversion = ValueType::Float;
            if (rhs.resolvedType == ValueType::Int) rhs.conversion = ValueType::Float;
            return ValueType::Float;
        }
        return ValueType::Int;
    }

    // Two references may be compared when one of them could have been
    // stored in the other, which is exactly when they can name the same
    // object. `null` compares with any of them.
    bool CanCompareReferences(Expr& lhs, Expr& rhs) const
    {
        if (lhs.resolvedType == ValueType::Null || rhs.resolvedType == ValueType::Null) return true;
        if (lhs.resolvedType != ValueType::Object || rhs.resolvedType != ValueType::Object) return false;

        TypeRef lhsType;
        lhsType.type = ValueType::Object;
        lhsType.classIndex = lhs.resolvedClass;

        TypeRef rhsType;
        rhsType.type = ValueType::Object;
        rhsType.classIndex = rhs.resolvedClass;

        return IsReferenceAssignable(lhs, rhsType) || IsReferenceAssignable(rhs, lhsType);
    }

    void AnalyzeBinary(BinaryExpr& expr)
    {
        AnalyzeExpr(*expr.lhs);
        AnalyzeExpr(*expr.rhs);

        const ValueType lhsType = expr.lhs->resolvedType;
        const ValueType rhsType = expr.rhs->resolvedType;
        const bool poisoned = IsPoisoned(lhsType) || IsPoisoned(rhsType);

        switch (expr.op)
        {
            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr: {
                if (!poisoned && (lhsType != ValueType::Bool || rhsType != ValueType::Bool))
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs 'bool' operands, found " +
                                             Quoted(*expr.lhs) + " and " + Quoted(*expr.rhs));
                expr.operandType = ValueType::Bool;
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Equal:
            case BinaryOp::NotEqual: {
                if (IsReference(lhsType) || IsReference(rhsType))
                {
                    if (CanCompareReferences(*expr.lhs, *expr.rhs)) expr.operandType = ValueType::Object;
                    else expr.operandType = ValueType::Unknown;
                }
                else if (lhsType == ValueType::Bool && rhsType == ValueType::Bool) expr.operandType = ValueType::Bool;
                else if (lhsType == ValueType::String && rhsType == ValueType::String) expr.operandType = ValueType::String;
                else expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);

                if (IsPoisoned(expr.operandType) && !poisoned)
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' cannot compare " +
                                             Quoted(*expr.lhs) + " with " + Quoted(*expr.rhs));
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Less:
            case BinaryOp::Greater:
            case BinaryOp::LessEqual:
            case BinaryOp::GreaterEqual: {
                expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);
                if (IsPoisoned(expr.operandType) && !poisoned)
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs numeric operands, found " +
                                             Quoted(*expr.lhs) + " and " + Quoted(*expr.rhs));
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Add: {
                // A string on the left absorbs whatever is on the right,
                // turning it into text.
                if (lhsType == ValueType::String)
                {
                    if (rhsType != ValueType::String && !CanStringify(rhsType))
                    {
                        if (!poisoned)
                            Error(expr.location, "'+' cannot join a 'string' with " + Quoted(*expr.rhs));
                    }
                    else if (rhsType != ValueType::String)
                    {
                        expr.rhs->conversion = ValueType::String;
                    }
                    expr.operandType = ValueType::String;
                    expr.resolvedType = ValueType::String;
                    return;
                }
                AnalyzeArithmetic(expr, poisoned);
                return;
            }

            case BinaryOp::Sub:
            case BinaryOp::Mul:
            case BinaryOp::Div:
            case BinaryOp::Mod:
                AnalyzeArithmetic(expr, poisoned);
                return;

            default:
                expr.resolvedType = ValueType::Unknown;
                return;
        }
    }

    void AnalyzeArithmetic(BinaryExpr& expr, bool poisoned)
    {
        expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);
        if (IsPoisoned(expr.operandType))
        {
            if (!poisoned)
                Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs numeric operands, found " +
                                         Quoted(*expr.lhs) + " and " + Quoted(*expr.rhs));
            expr.resolvedType = ValueType::Unknown;
            return;
        }
        expr.resolvedType = expr.operandType;
    }

    static bool CanStringify(ValueType type)
    {
        return type == ValueType::Int || type == ValueType::Float || type == ValueType::Bool || type == ValueType::String;
    }

    static const char* OperatorText(BinaryOp op)
    {
        switch (op)
        {
            case BinaryOp::Add: return "+";
            case BinaryOp::Sub: return "-";
            case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";
            case BinaryOp::Mod: return "%";
            case BinaryOp::Equal: return "==";
            case BinaryOp::NotEqual: return "!=";
            case BinaryOp::Less: return "<";
            case BinaryOp::Greater: return ">";
            case BinaryOp::LessEqual: return "<=";
            case BinaryOp::GreaterEqual: return ">=";
            case BinaryOp::LogicalAnd: return "&&";
            case BinaryOp::LogicalOr: return "||";
            default: return "?";
        }
    }

    void AnalyzeAssign(AssignExpr& expr)
    {
        const bool targetIsName = expr.target->kind == ExprKind::Identifier;
        const bool targetIsMember = expr.target->kind == ExprKind::Member;

        if (!targetIsName && !targetIsMember)
        {
            Error(expr.location, "the left-hand side of an assignment must be a variable or a field");
            AnalyzeExpr(*expr.value);
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        AnalyzeExpr(*expr.target);

        if (targetIsMember && static_cast<MemberExpr&>(*expr.target).binding != MemberBinding::Field &&
            !IsPoisoned(expr.target->resolvedType))
        {
            Error(expr.location, "the left-hand side of an assignment must be a variable or a field");
            AnalyzeExpr(*expr.value);
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        AnalyzeExpr(*expr.value);

        TypeRef targetType;
        targetType.type = expr.target->resolvedType;
        targetType.classIndex = expr.target->resolvedClass;

        const ValueType valueType = expr.value->resolvedType;
        expr.resolvedType = targetType.type;
        expr.resolvedClass = targetType.classIndex;

        if (expr.op == AssignOp::Assign)
        {
            CheckAssignable(*expr.value, targetType, "in an assignment");
            return;
        }

        const bool poisoned = IsPoisoned(targetType.type) || IsPoisoned(valueType);

        // `s += x` appends text; every other compound form is arithmetic.
        if (expr.op == AssignOp::AddAssign && targetType.type == ValueType::String)
        {
            if (valueType != ValueType::String && !CanStringify(valueType))
            {
                if (!poisoned) Error(expr.location, "'+=' cannot append " + Quoted(*expr.value) + " to a 'string'");
            }
            else if (valueType != ValueType::String)
            {
                expr.value->conversion = ValueType::String;
            }
            expr.operandType = ValueType::String;
            return;
        }

        if (!IsNumeric(targetType.type) || !IsNumeric(valueType))
        {
            if (!poisoned)
                Error(expr.location, std::string("'") + CompoundOperatorText(expr.op) + "' needs numeric operands, found " +
                                         Quoted(targetType) + " and " + Quoted(*expr.value));
            expr.operandType = ValueType::Unknown;
            return;
        }

        if (targetType.type == ValueType::Float)
        {
            if (valueType == ValueType::Int) expr.value->conversion = ValueType::Float;
            expr.operandType = ValueType::Float;
            return;
        }

        // An int target cannot absorb a float without losing information.
        if (valueType == ValueType::Float)
        {
            Error(expr.location, "cannot convert 'float' to 'int' in a compound assignment");
            expr.operandType = ValueType::Unknown;
            return;
        }
        expr.operandType = ValueType::Int;
    }

    static const char* CompoundOperatorText(AssignOp op)
    {
        switch (op)
        {
            case AssignOp::AddAssign: return "+=";
            case AssignOp::SubAssign: return "-=";
            case AssignOp::MulAssign: return "*=";
            case AssignOp::DivAssign: return "/=";
            default: return "=";
        }
    }

    // --- Calls ----------------------------------------------------------------

    static std::string DescribeCallee(const MemberExpr& member)
    {
        if (member.base && member.base->kind == ExprKind::Identifier)
            return static_cast<const IdentifierExpr*>(member.base.get())->name + "." + member.member;
        if (member.base && member.base->kind == ExprKind::This)
            return "this." + member.member;
        return "." + member.member;
    }

    void AnalyzeCall(CallExpr& call)
    {
        for (ExprPtr& arg : call.args) AnalyzeExpr(*arg);

        if (!call.callee)
        {
            call.resolvedType = ValueType::Unknown;
            return;
        }

        if (call.callee->kind == ExprKind::Identifier)
        {
            AnalyzeUnqualifiedCall(call);
            return;
        }

        if (call.callee->kind == ExprKind::Member)
        {
            AnalyzeQualifiedCall(call, static_cast<MemberExpr&>(*call.callee));
            return;
        }

        Error(call.location, "this expression cannot be called");
        call.resolvedType = ValueType::Unknown;
    }

    // `Name(args)`: a method of the class the call is written in, reached
    // through the surrounding instance when it needs one.
    void AnalyzeUnqualifiedCall(CallExpr& call)
    {
        const auto& callee = static_cast<const IdentifierExpr&>(*call.callee);

        MethodDecl* method = m_currentClass ? FindMethodInChain(m_currentClass->index, callee.name) : nullptr;
        if (!method)
        {
            Error(call.location, "no method named '" + callee.name + "' exists here");
            call.resolvedType = ValueType::Unknown;
            return;
        }

        if (method->isStatic)
        {
            BindStaticCall(call, *method, callee.name);
            return;
        }

        if (!m_currentMethodIsInstance)
        {
            Error(call.location, "'" + method->qualifiedName + "' belongs to an instance and cannot be called from a static method without one");
            call.resolvedType = ValueType::Unknown;
            return;
        }

        auto self = std::make_unique<ThisExpr>();
        self->location = call.location;
        self->resolvedType = ValueType::Object;
        self->resolvedClass = m_currentClass->index;
        call.receiver = std::move(self);

        BindInstanceCall(call, *method, m_currentClass->index, callee.name);
    }

    // `A.Name(args)`: a built-in, a static method named through its
    // class, or an instance method reached through a value.
    void AnalyzeQualifiedCall(CallExpr& call, MemberExpr& callee)
    {
        const std::string qualified = DescribeCallee(callee);

        u32 scopeClass = kNoClass;
        if (NamesAClass(*callee.base, scopeClass))
        {
            callee.binding = MemberBinding::Scope;

            MethodDecl* method = FindMethodInChain(scopeClass, callee.member);
            if (!method)
            {
                Error(call.location, "'" + ClassName(scopeClass) + "' has no method named '" + callee.member + "'");
                call.resolvedType = ValueType::Unknown;
                return;
            }
            if (!method->isStatic)
            {
                Error(call.location, "'" + method->qualifiedName + "' belongs to an instance, so it cannot be called through the type name");
                call.resolvedType = ValueType::Unknown;
                return;
            }
            BindStaticCall(call, *method, qualified);
            return;
        }

        // A built-in is named the same way but belongs to no declared
        // class, so it is what remains once the base is known not to be
        // a value either.
        if (callee.base->kind == ExprKind::Identifier && !IsKnownValueName(*callee.base))
        {
            auto native = m_natives.find(qualified);
            if (native != m_natives.end())
            {
                callee.binding = MemberBinding::Scope;
                BindNative(call, native->second, qualified);
                return;
            }
        }

        AnalyzeExpr(*callee.base);
        if (IsPoisoned(callee.base->resolvedType))
        {
            call.resolvedType = ValueType::Unknown;
            return;
        }
        if (callee.base->resolvedType != ValueType::Object)
        {
            Error(call.location, Quoted(*callee.base) + " has no method named '" + callee.member + "'");
            call.resolvedType = ValueType::Unknown;
            return;
        }

        const u32 receiverClass = callee.base->resolvedClass;
        MethodDecl* method = FindMethodInChain(receiverClass, callee.member);
        if (!method)
        {
            Error(call.location, "'" + ClassName(receiverClass) + "' has no method named '" + callee.member + "'");
            call.resolvedType = ValueType::Unknown;
            return;
        }
        if (method->isStatic)
        {
            Error(call.location, "'" + method->qualifiedName + "' is static, so it cannot be called through an instance");
            call.resolvedType = ValueType::Unknown;
            return;
        }

        call.receiver = std::move(callee.base);
        BindInstanceCall(call, *method, receiverClass, qualified);
    }

    bool IsKnownValueName(const Expr& expr) const
    {
        if (expr.kind != ExprKind::Identifier) return true;
        const auto& identifier = static_cast<const IdentifierExpr&>(expr);

        LocalSymbol symbol;
        if (Lookup(identifier.name, symbol)) return true;
        return m_currentClass && FindField(m_currentClass->index, identifier.name) != nullptr;
    }

    void BindStaticCall(CallExpr& call, MethodDecl& method, const std::string& displayName)
    {
        call.target = CallTarget::ScriptMethod;
        call.targetIndex = method.functionIndex;
        CompleteScriptCall(call, method, displayName);
    }

    void BindInstanceCall(CallExpr& call, MethodDecl& method, u32 receiverClass, const std::string& displayName)
    {
        call.targetIndex = method.functionIndex;

        if (receiverClass < m_classes.size() && m_classes[receiverClass].decl->isInterface)
            call.target = CallTarget::InterfaceMethod;
        else if (method.vtableSlot != kNoVtableSlot)
            call.target = CallTarget::VirtualMethod;
        else
            call.target = CallTarget::InstanceMethod;

        CompleteScriptCall(call, method, displayName);
    }

    void CompleteScriptCall(CallExpr& call, MethodDecl& method, const std::string& displayName)
    {
        call.resolvedType = method.returnType.type;
        call.resolvedClass = method.returnType.classIndex;

        if (call.args.size() != method.params.size())
        {
            Error(call.location, "method '" + displayName + "' takes " + std::to_string(method.params.size()) +
                                     " argument(s) but " + std::to_string(call.args.size()) + " were given");
            return;
        }

        for (size_t i = 0; i < call.args.size(); ++i)
            CheckAssignable(*call.args[i], method.params[i].type, "in argument " + std::to_string(i + 1) + " of '" + displayName + "'");
    }

    void BindNative(CallExpr& call, const std::vector<NativeFunctionId>& candidates, const std::string& displayName)
    {
        // A built-in takes at most one argument, so choosing between the
        // entries sharing a name is purely a question of that argument's
        // type.
        call.target = CallTarget::Native;
        call.resolvedType = ValueType::Unknown;

        const NativeFunctionSignature* natives = NativeFunctionTable();

        if (call.args.empty())
        {
            for (NativeFunctionId candidate : candidates)
            {
                if (natives[(u32)candidate].parameterType != ValueType::Void) continue;
                call.targetIndex = (u32)candidate;
                call.resolvedType = natives[(u32)candidate].returnType;
                return;
            }
            Error(call.location, "'" + displayName + "' takes 1 argument but none were given");
            return;
        }

        if (call.args.size() != 1)
        {
            Error(call.location, "'" + displayName + "' takes 1 argument but " + std::to_string(call.args.size()) + " were given");
            return;
        }

        const ValueType argType = call.args[0]->resolvedType;

        for (NativeFunctionId candidate : candidates)
        {
            if (natives[(u32)candidate].parameterType != argType) continue;
            call.targetIndex = (u32)candidate;
            call.resolvedType = natives[(u32)candidate].returnType;
            return;
        }

        // No exact form; fall back to the single implicit widening.
        if (argType == ValueType::Int)
        {
            for (NativeFunctionId candidate : candidates)
            {
                if (natives[(u32)candidate].parameterType != ValueType::Float) continue;
                call.targetIndex = (u32)candidate;
                call.resolvedType = natives[(u32)candidate].returnType;
                call.args[0]->conversion = ValueType::Float;
                return;
            }
        }

        if (!IsPoisoned(argType))
            Error(call.args[0]->location, "'" + displayName + "' does not accept an argument of type " + Quoted(*call.args[0]));
    }
};

} // namespace

bool Analyze(Program& program, DiagnosticList& diagnostics)
{
    Analyzer analyzer(program, diagnostics);
    return analyzer.Run();
}

} // namespace Fluxion::Script
