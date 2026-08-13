#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <Compiler/AstClone.hpp>

#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fluxion::Script
{

namespace
{

// How deeply one set of type arguments may nest inside another. A
// declaration that names itself with a type argument built out of its own
// parameter grows this by one every time, so a cap is what turns an
// endless expansion into a message.
constexpr u32 kMaxTypeArgumentDepth = 8;

// A backstop on how many declarations the analyzer may create for itself,
// so a program that expands widely rather than deeply is also stopped.
constexpr u32 kMaxGeneratedClasses = 1024;

bool IsNumeric(ValueType type) { return type == ValueType::Int || type == ValueType::Float; }

// True once an error has already been reported for the expression that
// produced this type, so further complaints about it would only be noise.
bool IsPoisoned(ValueType type) { return type == ValueType::Unknown; }

bool IsReference(ValueType type) { return type == ValueType::Object || type == ValueType::Null; }

class Analyzer
{
public:
    Analyzer(Program& program, DiagnosticList& diagnostics, const BindingTable* bindings)
        : m_program(program), m_diagnostics(diagnostics), m_bindings(bindings)
    {
    }

    bool Run()
    {
        const NativeFunctionSignature* natives = NativeFunctionTable();
        for (u32 i = 0; i < kNativeFunctionCount; ++i)
            m_natives[natives[i].qualifiedName].push_back((NativeFunctionId)i);

        CollectClasses();
        CheckTypeParameterNames();
        if (m_diagnostics.HasErrors()) return false;

        // The walks below are all by index over a container that grows
        // while they run: naming a type that does not exist yet appends
        // the declaration for it, and the walk reaches that declaration
        // on a later turn without anything having to schedule it.
        for (u32 i = 0; i < (u32)m_classes.size(); ++i) PrepareClass(i);
        if (m_diagnostics.HasErrors()) return false;

        for (u32 i = 0; i < (u32)m_classes.size(); ++i) BuildLayout(i);
        if (m_diagnostics.HasErrors()) return false;

        for (u32 i = 0; i < (u32)m_classes.size(); ++i)
        {
            if (m_classes[i].isArray) continue;

            m_currentClass = &m_classes[i];
            ClassDecl* decl = m_classes[i].decl;
            for (size_t m = 0; m < decl->methods.size(); ++m)
                AnalyzeMethod(*static_cast<MethodDecl*>(decl->methods[m].get()));
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

        // An indexable sequence rather than a declaration the source
        // wrote: it has no members, no layout to build and no bodies to
        // analyze, only the type of the elements it holds.
        bool isArray = false;
        TypeRef elementType;

        // How deeply type arguments nest inside this type's own name.
        // Zero for anything the source wrote out.
        u32 typeArgumentDepth = 0;

        // Set once the declaration has a base list, function numbers and
        // resolved member types, so a type that names itself part-way
        // through does not start over.
        bool prepared = false;

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

        // Names a construct writes for itself each time round, which the
        // body it encloses may read but never store into.
        bool readOnly = false;
    };

    // What a frame slot has been used for. A slot is never shared between
    // a reference and a plain value, so the bitmap that drives collection
    // stays exact even though sibling scopes reuse slots.
    enum class SlotUse : u8 { Free, Value, Reference };

    Program& m_program;
    DiagnosticList& m_diagnostics;

    // The engine types this source may reach, handed in by whoever asked
    // for the compilation. Null when it may reach none.
    const BindingTable* m_bindings = nullptr;

    // A deque rather than a vector: naming a type appends a declaration
    // in the middle of walking an existing one, and every reference and
    // pointer taken into this container has to keep meaning what it did.
    std::deque<ClassEntry> m_classes;
    std::unordered_map<std::string, u32> m_classByName;
    std::unordered_map<std::string, MethodDecl*> m_methods; // keyed "Class.Method"
    std::unordered_map<std::string, std::vector<NativeFunctionId>> m_natives;

    // Declarations written with type parameters, kept apart from the
    // classes: a pattern has no index, no layout and no instructions --
    // only the concrete copies made from it do.
    std::unordered_map<std::string, ClassDecl*> m_templates;

    u32 m_nextFunctionIndex = 0;
    u32 m_generatedClasses = 0;

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
        if (type == ValueType::Handle)
        {
            if (m_bindings && classIndex < m_bindings->types.size()) return "'" + m_bindings->types[classIndex].name + "'";
            return "an engine handle";
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

            const bool alreadyTaken = m_classByName.find(classDecl->name) != m_classByName.end() ||
                                      m_templates.find(classDecl->name) != m_templates.end();
            if (alreadyTaken)
                Error(classDecl->location, "'" + classDecl->name + "' is declared more than once");

            if (!classDecl->typeParams.empty())
            {
                m_templates.emplace(classDecl->name, classDecl);
                continue;
            }

            RegisterClass(classDecl, classDecl->name, 0);
        }
    }

    // A parameter name that also names a declared type would silently
    // take that type's place everywhere inside the declaration, so it is
    // refused rather than resolved one way or the other.
    void CheckTypeParameterNames()
    {
        for (const auto& entry : m_templates)
        {
            ClassDecl& decl = *entry.second;
            for (size_t i = 0; i < decl.typeParams.size(); ++i)
            {
                const std::string& name = decl.typeParams[i];
                const SourceLocation& location = decl.typeParamLocations[i];

                u32 ignored = kNoClass;
                if (FindClass(name, ignored) || m_templates.find(name) != m_templates.end())
                {
                    Error(location, "type parameter '" + name + "' has the same name as a declared type");
                    continue;
                }

                bool duplicate = false;
                for (size_t j = 0; j < i; ++j) duplicate = duplicate || decl.typeParams[j] == name;
                if (duplicate)
                    Error(location, "'" + decl.name + "' declares the type parameter '" + name + "' more than once");
            }
        }
    }

    bool FindClass(const std::string& name, u32& outIndex) const
    {
        auto found = m_classByName.find(name);
        if (found == m_classByName.end()) return false;
        outIndex = found->second;
        return true;
    }

    u32 RegisterClass(ClassDecl* decl, const std::string& name, u32 typeArgumentDepth)
    {
        const u32 index = (u32)m_classes.size();
        decl->classIndex = index;

        ClassEntry entry;
        entry.decl = decl;
        entry.index = index;
        entry.typeArgumentDepth = typeArgumentDepth;
        m_classes.push_back(std::move(entry));

        // A repeated name keeps naming whichever declaration came first;
        // the duplicate was already reported where it was found.
        m_classByName.emplace(name, index);
        return index;
    }

    // Everything a declaration needs before anything may be laid out on
    // top of it or written against it. Runs once per declaration, whether
    // the source wrote it or a set of type arguments produced it.
    void PrepareClass(u32 index)
    {
        if (m_classes[index].prepared) return;
        m_classes[index].prepared = true;
        if (m_classes[index].isArray) return;

        ResolveBaseList(index);
        CheckClassShape(index);
        SynthesizeConstructor(index);
        AssignFunctionIndices(index);
        ResolveMemberTypes(index);
    }

    void ResolveBaseList(u32 index)
    {
        ClassDecl& decl = *m_classes[index].decl;

        if (decl.isInterface && !decl.baseTypes.empty())
        {
            Error(decl.baseLocations[0], "interface '" + decl.name + "' cannot declare a base list");
            return;
        }
        if (decl.isStatic && !decl.baseTypes.empty())
        {
            Error(decl.baseLocations[0], "'" + decl.name + "' holds only static members, so it cannot declare a base list");
            return;
        }

        for (size_t i = 0; i < decl.baseTypes.size(); ++i)
        {
            const SourceLocation location = decl.baseLocations[i];
            const std::string written = DescribeWrittenType(decl.baseTypes[i]);

            if (decl.baseTypes[i].arrayDepth != 0)
            {
                Error(location, "'" + written + "' is a sequence, and nothing can be built on one");
                continue;
            }
            if (!ResolveTypeRef(decl.baseTypes[i], location, "so '" + decl.name + "' has no such base type")) continue;

            const u32 target = decl.baseTypes[i].classIndex;
            if (target == kNoClass) continue;
            if (target == index)
            {
                Error(location, "'" + decl.name + "' cannot list itself as a base type");
                continue;
            }
            if (m_classes[target].isArray)
            {
                Error(location, "'" + ClassName(target) + "' is a sequence, and nothing can be built on one");
                continue;
            }

            if (m_classes[target].decl->isInterface)
            {
                bool duplicate = false;
                for (u32 existing : m_classes[index].declaredInterfaces) duplicate = duplicate || existing == target;
                if (duplicate)
                    Error(location, "'" + decl.name + "' lists interface '" + ClassName(target) + "' more than once");
                else
                    m_classes[index].declaredInterfaces.push_back(target);
                continue;
            }

            if (i != 0)
            {
                Error(location, "base class '" + ClassName(target) + "' must come first in the base list of '" + decl.name + "'");
                continue;
            }
            m_classes[index].base = target;
        }
    }

    void CheckClassShape(u32 index)
    {
        ClassDecl& decl = *m_classes[index].decl;

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

    // Every class that can be instantiated ends up with a constructor,
    // so a `new` and a base chain never have to special-case its absence.
    void SynthesizeConstructor(u32 index)
    {
        ClassDecl& decl = *m_classes[index].decl;
        if (decl.isInterface || decl.isStatic) return;

        for (DeclPtr& methodDecl : decl.methods)
        {
            auto* method = static_cast<MethodDecl*>(methodDecl.get());
            if (!method->isConstructor) continue;
            if (m_classes[index].constructor)
            {
                Error(method->location, "'" + decl.name + "' declares more than one constructor");
                continue;
            }
            m_classes[index].constructor = method;
        }
        if (m_classes[index].constructor) return;

        auto synthesized = std::make_unique<MethodDecl>();
        synthesized->location = decl.location;
        synthesized->name = decl.name;
        synthesized->isConstructor = true;
        synthesized->returnType.type = ValueType::Void;
        synthesized->body = std::make_unique<BlockStmt>();
        synthesized->body->location = decl.location;

        m_classes[index].constructor = synthesized.get();
        decl.methods.push_back(std::move(synthesized));
    }

    // Every method is numbered before any body is looked at, so a call
    // can be resolved against a method declared later in the file.
    void AssignFunctionIndices(u32 index)
    {
        ClassDecl& decl = *m_classes[index].decl;

        for (DeclPtr& methodDecl : decl.methods)
        {
            auto* method = static_cast<MethodDecl*>(methodDecl.get());
            method->owningClass = index;
            method->qualifiedName = decl.name + "." + method->name;
            method->functionIndex = m_nextFunctionIndex++;

            if (method->isConstructor) continue;
            if (!m_methods.emplace(method->qualifiedName, method).second)
                Error(method->location, "method '" + method->qualifiedName + "' is declared more than once");
        }
        if (m_classes[index].constructor) decl.constructorFunction = m_classes[index].constructor->functionIndex;
    }

    void ResolveMemberTypes(u32 index)
    {
        ClassDecl& decl = *m_classes[index].decl;

        for (DeclPtr& fieldDecl : decl.fields)
        {
            auto* field = static_cast<FieldDecl*>(fieldDecl.get());
            if (field->type.type == ValueType::Void && field->type.arrayDepth == 0)
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
                if (param.type.type == ValueType::Void && param.type.arrayDepth == 0)
                {
                    Error(param.location, "parameter '" + param.name + "' cannot have type 'void'");
                    param.type.type = ValueType::Unknown;
                    continue;
                }
                ResolveTypeRef(param.type, param.location, "so parameter '" + param.name + "' has no type");
            }
        }
    }

    // --- Naming, sequences and type arguments --------------------------------

    // What a resolved type is called, which is also what a generated
    // declaration is named after.
    std::string TypeName(const TypeRef& resolved) const
    {
        if (resolved.type == ValueType::Object)
        {
            if (resolved.classIndex < m_classes.size()) return m_classes[resolved.classIndex].decl->name;
            return "<unknown>";
        }
        if (resolved.type == ValueType::Handle)
        {
            if (m_bindings && resolved.classIndex < m_bindings->types.size()) return m_bindings->types[resolved.classIndex].name;
            return "<unknown>";
        }
        return ValueTypeName(resolved.type);
    }

    // An engine type the host made visible, if the written name is one.
    u32 BoundTypeNamed(const std::string& name) const
    {
        if (!m_bindings) return kNoBoundType;
        return FindBoundType(*m_bindings, name);
    }

    // What the source wrote, for a message about a type that could not be
    // resolved and therefore has no name of its own yet.
    static std::string DescribeWrittenType(const TypeRef& written)
    {
        std::string text = (written.type == ValueType::Object) ? written.name : ValueTypeName(written.type);
        if (!written.typeArgs.empty())
        {
            text += "<";
            for (size_t i = 0; i < written.typeArgs.size(); ++i)
            {
                if (i != 0) text += ", ";
                text += DescribeWrittenType(written.typeArgs[i]);
            }
            text += ">";
        }
        for (u32 i = 0; i < written.arrayDepth; ++i) text += "[]";
        return text;
    }

    u32 TypeArgumentDepth(const TypeRef& resolved) const
    {
        if (resolved.type != ValueType::Object || resolved.classIndex >= m_classes.size()) return 0;
        return m_classes[resolved.classIndex].typeArgumentDepth;
    }

    // A sequence of one element type is a declaration like any other: it
    // gets an index, a name and a place in the class table, so a
    // collection can ask it whether its elements are references.
    u32 GetOrCreateArrayClass(const TypeRef& element, const SourceLocation& location)
    {
        const std::string name = TypeName(element) + "[]";

        u32 existing = kNoClass;
        if (FindClass(name, existing)) return existing;

        if (m_generatedClasses >= kMaxGeneratedClasses)
        {
            Error(location, "this program needs more generated types than one module may hold");
            return kNoClass;
        }

        auto decl = std::make_unique<ClassDecl>();
        decl->location = location;
        decl->name = name;
        decl->isArray = true;
        decl->arrayElementType = element;
        decl->arrayElementIsReference = element.type == ValueType::Object;

        ClassDecl* raw = decl.get();
        const u32 index = RegisterClass(raw, name, TypeArgumentDepth(element));
        m_classes[index].isArray = true;
        m_classes[index].elementType = element;
        m_classes[index].prepared = true;
        m_classes[index].layoutState = 2;

        m_program.declarations.push_back(std::move(decl));
        ++m_generatedClasses;
        return index;
    }

    // Turns a pattern plus one set of arguments into a declaration of its
    // own: its own fields, its own field bitmap, its own tables and its
    // own copy of every method body. Registering the name before the copy
    // is prepared is what lets a declaration mention itself.
    u32 Instantiate(ClassDecl& pattern, const std::vector<TypeRef>& args, const SourceLocation& location)
    {
        if (args.size() != pattern.typeParams.size())
        {
            Error(location, "'" + pattern.name + "' takes " + std::to_string(pattern.typeParams.size()) +
                                " type argument(s) but " + std::to_string(args.size()) + " were given");
            return kNoClass;
        }

        std::string name = pattern.name + "<";
        u32 depth = 0;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i != 0) name += ", ";
            name += TypeName(args[i]);

            const u32 argumentDepth = TypeArgumentDepth(args[i]);
            if (argumentDepth > depth) depth = argumentDepth;
        }
        name += ">";
        ++depth;

        u32 existing = kNoClass;
        if (FindClass(name, existing)) return existing;

        // A declaration whose own field names it again with a deeper
        // argument would go on producing new ones forever; stopping at a
        // fixed depth turns that into something a reader can act on.
        if (depth > kMaxTypeArgumentDepth)
        {
            Error(location, "'" + name + "' nests type arguments more than " + std::to_string(kMaxTypeArgumentDepth) +
                                " deep, which is what a declaration that builds on itself without end looks like");
            return kNoClass;
        }
        if (m_generatedClasses >= kMaxGeneratedClasses)
        {
            Error(location, "this program needs more generated types than one module may hold");
            return kNoClass;
        }

        std::unique_ptr<ClassDecl> copy = CloneClassDecl(pattern);
        copy->name = name;
        copy->typeParams.clear();
        copy->typeParamLocations.clear();

        std::unordered_map<std::string, TypeRef> substitution;
        for (size_t i = 0; i < args.size(); ++i) substitution.emplace(pattern.typeParams[i], args[i]);
        SubstituteTypeParameters(*copy, substitution);

        ClassDecl* raw = copy.get();
        const u32 index = RegisterClass(raw, name, depth);
        m_program.declarations.push_back(std::move(copy));
        ++m_generatedClasses;

        PrepareClass(index);
        BuildLayout(index);
        return index;
    }

    // Resolves the name at the head of a written type, creating the
    // declaration it names if a set of type arguments has not been seen
    // before.
    bool ResolveNamedType(const TypeRef& written, const SourceLocation& location, const std::string& what, u32& outIndex)
    {
        auto pattern = m_templates.find(written.name);
        if (pattern != m_templates.end())
        {
            if (written.typeArgs.empty())
            {
                Error(location, "'" + written.name + "' is declared with type parameters, so it needs type arguments to name a type");
                return false;
            }

            std::vector<TypeRef> args;
            args.reserve(written.typeArgs.size());
            for (const TypeRef& argument : written.typeArgs)
            {
                TypeRef resolved = argument;
                if (!ResolveTypeRef(resolved, location, what)) return false;
                if (resolved.type == ValueType::Void)
                {
                    Error(location, "'void' cannot be used as a type argument");
                    return false;
                }
                args.push_back(std::move(resolved));
            }

            outIndex = Instantiate(*pattern->second, args, location);
            return outIndex != kNoClass;
        }

        u32 index = kNoClass;
        if (!FindClass(written.name, index))
        {
            Error(location, "no type named '" + written.name + "' is declared, " + what);
            return false;
        }
        if (!written.typeArgs.empty())
        {
            Error(location, "'" + written.name + "' is not declared with type parameters, so it cannot be given type arguments");
            return false;
        }
        if (m_classes[index].decl->isStatic)
        {
            Error(location, "'" + written.name + "' holds only static members, so nothing can have it as a type");
            return false;
        }

        outIndex = index;
        return true;
    }

    // Turns a written type into a concrete one. Afterwards the reference
    // names exactly one declaration or one built-in, and carries no type
    // arguments and no brackets of its own -- so resolving an already
    // resolved type is the same answer again.
    bool ResolveTypeRef(TypeRef& type, const SourceLocation& location, const std::string& what)
    {
        if (IsPoisoned(type.type)) return false;

        // An already-resolved handle type resolves to itself, exactly as
        // an already-resolved class does.
        if (type.type == ValueType::Handle) return true;

        // A name the host made visible is a handle to something the
        // engine owns. A declaration written in the source wins the name,
        // so nothing a program declares can be taken away by what the
        // host happens to expose.
        if (type.type == ValueType::Object && type.typeArgs.empty())
        {
            u32 declared = kNoClass;
            const bool nameIsDeclared = FindClass(type.name, declared) || m_templates.find(type.name) != m_templates.end();
            const u32 bound = nameIsDeclared ? kNoBoundType : BoundTypeNamed(type.name);
            if (bound != kNoBoundType)
            {
                TypeRef resolved;
                resolved.type = ValueType::Handle;
                resolved.classIndex = bound;
                resolved.name = m_bindings->types[bound].name;

                for (u32 i = 0; i < type.arrayDepth; ++i)
                {
                    const u32 arrayClass = GetOrCreateArrayClass(resolved, location);
                    if (arrayClass == kNoClass)
                    {
                        type.type = ValueType::Unknown;
                        type.arrayDepth = 0;
                        return false;
                    }

                    resolved = TypeRef{};
                    resolved.type = ValueType::Object;
                    resolved.classIndex = arrayClass;
                    resolved.name = ClassName(arrayClass);
                }

                type = std::move(resolved);
                return true;
            }
        }

        u32 baseClass = kNoClass;
        if (type.type == ValueType::Object && !ResolveNamedType(type, location, what, baseClass))
        {
            type.type = ValueType::Unknown;
            type.typeArgs.clear();
            type.arrayDepth = 0;
            return false;
        }

        if (type.arrayDepth != 0 && type.type == ValueType::Void)
        {
            Error(location, "there is no sequence of 'void'");
            type.type = ValueType::Unknown;
            type.arrayDepth = 0;
            return false;
        }

        TypeRef resolved;
        resolved.type = type.type;
        resolved.classIndex = baseClass;
        if (type.type == ValueType::Object) resolved.name = TypeName(resolved);

        for (u32 i = 0; i < type.arrayDepth; ++i)
        {
            const u32 arrayClass = GetOrCreateArrayClass(resolved, location);
            if (arrayClass == kNoClass)
            {
                type.type = ValueType::Unknown;
                type.arrayDepth = 0;
                return false;
            }

            resolved = TypeRef{};
            resolved.type = ValueType::Object;
            resolved.classIndex = arrayClass;
            resolved.name = ClassName(arrayClass);
        }

        type = std::move(resolved);
        return true;
    }

    // --- Layout --------------------------------------------------------------

    void BuildLayout(u32 index)
    {
        ClassEntry& entry = m_classes[index];
        if (entry.isArray)
        {
            entry.layoutState = 2;
            return;
        }
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
        CollectInterfaces(index);
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

    // Every interface this class answers to, its base's included, worked
    // out before the tables that need to consult it.
    void CollectInterfaces(u32 index)
    {
        ClassEntry& entry = m_classes[index];

        if (entry.base != kNoClass) entry.allInterfaces = m_classes[entry.base].allInterfaces;
        for (u32 iface : entry.declaredInterfaces)
        {
            bool present = false;
            for (u32 existing : entry.allInterfaces) present = present || existing == iface;
            if (!present) entry.allInterfaces.push_back(iface);
        }
    }

    // Whether one of the interfaces this class answers to declares a
    // method of this name, which is the other reason a method may be
    // written 'override': it replaces nothing, but it is what a call
    // through the interface will reach.
    bool AnInterfaceDeclares(u32 index, const std::string& name) const
    {
        for (u32 iface : m_classes[index].allInterfaces)
        {
            if (FindMethodInChain(iface, name)) return true;
        }
        return false;
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
                    // Answering an interface is the other thing 'override'
                    // says. Such a method takes no table slot of its own:
                    // the class's table for that interface is what a call
                    // through it consults.
                    if (AnInterfaceDeclares(index, method->name)) continue;

                    Error(method->location, "'" + method->qualifiedName + "' is declared 'override', but neither a base class nor an interface declares a method named '" + method->name + "'");
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

    // A handle carries which engine type it names in the same place a
    // reference carries its class, so both kinds have to agree on that
    // index and not only on the value type.
    static bool SameType(const TypeRef& a, const TypeRef& b)
    {
        if (a.type != b.type) return false;
        if (a.type == ValueType::Object || a.type == ValueType::Handle) return a.classIndex == b.classIndex;
        return true;
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

    // The same question asked of two types rather than of an expression
    // and a type, which is what a loop variable needs: there is no node
    // standing for the element a walk produces.
    bool IsTypeAssignable(const TypeRef& from, const TypeRef& to) const
    {
        if (SameType(from, to)) return true;
        if (from.type != ValueType::Object || to.type != ValueType::Object) return false;
        if (from.classIndex >= m_classes.size() || to.classIndex >= m_classes.size()) return false;
        if (m_classes[to.classIndex].decl->isInterface) return ImplementsInterface(from.classIndex, to.classIndex);
        return IsDerivedFrom(from.classIndex, to.classIndex);
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

    int DeclareLocal(const std::string& name, const TypeRef& type, const SourceLocation& location, bool readOnly = false)
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

        scope.emplace(name, LocalSymbol{ type, slot, readOnly });
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

            case StmtKind::ForEach: AnalyzeForEach(static_cast<ForEachStmt&>(stmt)); break;

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
            if (stmt.declaredType.type == ValueType::Void && stmt.declaredType.arrayDepth == 0)
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

    // A counted walk, whatever it walks over. The sequence and the
    // position live in slots of the loop's own, so the sequence stays
    // reachable for as long as the loop runs and the body cannot reach
    // either of them by name.
    void AnalyzeForEach(ForEachStmt& stmt)
    {
        AnalyzeExpr(*stmt.sequence);

        PushScope();

        TypeRef elementType;
        elementType.type = ValueType::Unknown;

        const ValueType sequenceType = stmt.sequence->resolvedType;
        const u32 sequenceClass = stmt.sequence->resolvedClass;

        if (!IsPoisoned(sequenceType))
        {
            if (sequenceType != ValueType::Object)
            {
                Error(stmt.sequence->location, "'foreach' needs something with elements, found " + Quoted(*stmt.sequence));
            }
            else if (sequenceClass < m_classes.size() && m_classes[sequenceClass].isArray)
            {
                stmt.overArray = true;
                elementType = m_classes[sequenceClass].elementType;
            }
            else
            {
                stmt.overArray = false;
                BindSequenceMethods(stmt, sequenceClass, elementType);
            }
        }

        TypeRef holder;
        holder.type = ValueType::Object;
        holder.classIndex = (sequenceType == ValueType::Object) ? sequenceClass : kNoClass;
        stmt.sequenceSlot = DeclareLocal("$sequence", holder, stmt.location);

        TypeRef position;
        position.type = ValueType::Int;
        stmt.indexSlot = DeclareLocal("$position", position, stmt.location);

        if (stmt.declaredType.type == ValueType::Void && stmt.declaredType.arrayDepth == 0)
        {
            Error(stmt.location, "the loop variable '" + stmt.name + "' cannot have type 'void'");
            stmt.declaredType.type = ValueType::Unknown;
        }
        else
        {
            ResolveTypeRef(stmt.declaredType, stmt.location, "so the loop variable '" + stmt.name + "' has no type");
        }

        if (!IsPoisoned(elementType.type) && !IsPoisoned(stmt.declaredType.type) &&
            !IsTypeAssignable(elementType, stmt.declaredType))
        {
            Error(stmt.location, "cannot convert " + Quoted(elementType) + " to " + Quoted(stmt.declaredType) +
                                     " in the loop variable '" + stmt.name + "'");
        }

        stmt.loopSlot = DeclareLocal(stmt.name, stmt.declaredType, stmt.location, true);

        ++m_loopDepth;
        AnalyzeStmt(*stmt.body);
        --m_loopDepth;

        PopScope();
    }

    // What a type has to offer for a counted walk over it to mean
    // anything: how many elements there are, and how to reach one.
    void BindSequenceMethods(ForEachStmt& stmt, u32 sequenceClass, TypeRef& outElementType)
    {
        MethodDecl* count = FindMethodInChain(sequenceClass, "Count");
        MethodDecl* element = FindMethodInChain(sequenceClass, "Get");

        const bool countUsable = count && !count->isStatic && !count->isConstructor &&
                                 count->params.empty() && count->returnType.type == ValueType::Int;
        const bool elementUsable = element && !element->isStatic && !element->isConstructor &&
                                   element->params.size() == 1 && element->params[0].type.type == ValueType::Int &&
                                   element->returnType.type != ValueType::Void;

        if (!countUsable || !elementUsable)
        {
            Error(stmt.sequence->location, "'foreach' cannot walk " + Quoted(*stmt.sequence) +
                                               ": it is neither a sequence nor a type declaring 'int Count()' and a 'Get(int)' that answers with an element");
            return;
        }

        stmt.countTarget = DispatchKind(sequenceClass, *count);
        stmt.countFunction = count->functionIndex;
        stmt.elementTarget = DispatchKind(sequenceClass, *element);
        stmt.elementFunction = element->functionIndex;
        outElementType = element->returnType;
    }

    CallTarget DispatchKind(u32 receiverClass, const MethodDecl& method) const
    {
        if (receiverClass < m_classes.size() && m_classes[receiverClass].decl->isInterface) return CallTarget::InterfaceMethod;
        if (method.vtableSlot != kNoVtableSlot) return CallTarget::VirtualMethod;
        return CallTarget::InstanceMethod;
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

        // A handle goes only where a handle of the same engine type was
        // asked for. It is not a reference, so `null` is not one of the
        // things it can be, and it does not travel towards a less
        // specific type the way a reference does.
        if (target.type == ValueType::Handle || value.resolvedType == ValueType::Handle)
        {
            if (target.type == ValueType::Handle && value.resolvedType == ValueType::Handle &&
                target.classIndex == value.resolvedClass)
            {
                return true;
            }
            Error(value.location, "cannot convert " + Quoted(value) + " to " + Quoted(target) + " " + context);
            return false;
        }

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
            case ExprKind::NewArray: AnalyzeNewArray(static_cast<NewArrayExpr&>(expr)); break;
            case ExprKind::Identifier: AnalyzeIdentifier(static_cast<IdentifierExpr&>(expr)); break;
            case ExprKind::Member: AnalyzeMemberValue(static_cast<MemberExpr&>(expr)); break;
            case ExprKind::Index: AnalyzeIndex(static_cast<IndexExpr&>(expr)); break;
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

        const std::string written = DescribeWrittenType(expr.type);
        if (expr.type.type != ValueType::Object)
        {
            Error(expr.location, "'" + written + "' is not a type 'new' can create");
            return;
        }
        if (!ResolveTypeRef(expr.type, expr.location, "so there is nothing to create")) return;

        // An engine type is not something a script creates: the engine
        // owns its objects, and a script only ever holds a handle to one
        // it was given.
        if (expr.type.type != ValueType::Object)
        {
            Error(expr.location, "'" + written + "' belongs to the engine, so 'new' cannot create one");
            return;
        }

        const u32 index = expr.type.classIndex;
        const std::string& name = ClassName(index);

        ClassDecl& decl = *m_classes[index].decl;
        if (decl.isArray)
        {
            Error(expr.location, "'" + name + "' is a sequence, which is created with 'new' and an element count in brackets");
            return;
        }
        if (decl.isInterface)
        {
            Error(expr.location, "interface '" + name + "' cannot be created with 'new', only a class implementing it can");
            return;
        }
        if (decl.isStatic)
        {
            Error(expr.location, "'" + name + "' holds only static members and cannot be created with 'new'");
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
            Error(expr.location, "the constructor of '" + name + "' takes " + std::to_string(constructor->params.size()) +
                                     " argument(s) but " + std::to_string(expr.args.size()) + " were given");
            return;
        }
        for (size_t i = 0; i < expr.args.size(); ++i)
            CheckAssignable(*expr.args[i], constructor->params[i].type,
                "in argument " + std::to_string(i + 1) + " of the constructor of '" + name + "'");
    }

    void AnalyzeNewArray(NewArrayExpr& expr)
    {
        expr.resolvedType = ValueType::Unknown;

        AnalyzeExpr(*expr.length);
        if (expr.length->resolvedType != ValueType::Int && !IsPoisoned(expr.length->resolvedType))
            Error(expr.length->location, "the element count of a sequence must be 'int', found " + Quoted(*expr.length));

        if (expr.elementType.type == ValueType::Void && expr.elementType.arrayDepth == 0)
        {
            Error(expr.location, "there is no sequence of 'void'");
            return;
        }
        if (!ResolveTypeRef(expr.elementType, expr.location, "so this sequence has no element type")) return;

        expr.arrayClass = GetOrCreateArrayClass(expr.elementType, expr.location);
        if (expr.arrayClass == kNoClass) return;

        expr.resolvedType = ValueType::Object;
        expr.resolvedClass = expr.arrayClass;
    }

    void AnalyzeIndex(IndexExpr& expr)
    {
        AnalyzeExpr(*expr.base);
        AnalyzeExpr(*expr.index);

        expr.resolvedType = ValueType::Unknown;
        if (IsPoisoned(expr.base->resolvedType)) return;

        const u32 baseClass = expr.base->resolvedClass;
        if (expr.base->resolvedType != ValueType::Object || baseClass >= m_classes.size() || !m_classes[baseClass].isArray)
        {
            Error(expr.location, Quoted(*expr.base) + " has no elements to reach by position");
            return;
        }

        if (expr.index->resolvedType != ValueType::Int && !IsPoisoned(expr.index->resolvedType))
            Error(expr.index->location, "an element position must be 'int', found " + Quoted(*expr.index));

        const TypeRef& element = m_classes[baseClass].elementType;
        expr.resolvedType = element.type;
        expr.resolvedClass = element.classIndex;
    }

    void AnalyzeIdentifier(IdentifierExpr& expr)
    {
        LocalSymbol symbol;
        if (Lookup(expr.name, symbol))
        {
            expr.localSlot = symbol.slot;
            expr.isReadOnly = symbol.readOnly;
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

        // A sequence carries one thing worth reading besides its elements,
        // and it is the count it was created with.
        const u32 baseClass = expr.base->resolvedClass;
        if (baseClass < m_classes.size() && m_classes[baseClass].isArray)
        {
            if (expr.member != "Length")
            {
                Error(expr.location, "'" + ClassName(baseClass) + "' has no member named '" + expr.member + "'");
                expr.resolvedType = ValueType::Unknown;
                return;
            }
            expr.binding = MemberBinding::ArrayLength;
            expr.resolvedType = ValueType::Int;
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
        // Both sides have to be references before either can be `null`:
        // a value has no identity to compare, so there is nothing for
        // `null` to mean beside one.
        if (!IsReference(lhs.resolvedType) || !IsReference(rhs.resolvedType)) return false;
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
                // Two handles are compared when they name the same engine
                // type, and a handle is comparable with nothing else --
                // `null` included, since a handle never holds one.
                if (lhsType == ValueType::Handle || rhsType == ValueType::Handle)
                {
                    const bool sameEngineType = lhsType == ValueType::Handle && rhsType == ValueType::Handle &&
                                                expr.lhs->resolvedClass == expr.rhs->resolvedClass;
                    expr.operandType = sameEngineType ? ValueType::Handle : ValueType::Unknown;
                }
                else if (IsReference(lhsType) || IsReference(rhsType))
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
        const bool targetIsElement = expr.target->kind == ExprKind::Index;

        if (!targetIsName && !targetIsMember && !targetIsElement)
        {
            Error(expr.location, "the left-hand side of an assignment must be a variable, a field or an element");
            AnalyzeExpr(*expr.value);
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        AnalyzeExpr(*expr.target);

        if (targetIsName && static_cast<IdentifierExpr&>(*expr.target).isReadOnly)
        {
            Error(expr.location, "'" + static_cast<IdentifierExpr&>(*expr.target).name +
                                     "' is written by the loop it belongs to, so nothing inside the loop can assign to it");
            AnalyzeExpr(*expr.value);
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        if (targetIsMember && static_cast<MemberExpr&>(*expr.target).binding != MemberBinding::Field &&
            !IsPoisoned(expr.target->resolvedType))
        {
            Error(expr.location, "the left-hand side of an assignment must be a variable, a field or an element");
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
        // a value either. An engine type named through its own name is
        // reached the same way, and its static methods answer to it.
        if (callee.base->kind == ExprKind::Identifier && !IsKnownValueName(*callee.base))
        {
            auto native = m_natives.find(qualified);
            if (native != m_natives.end())
            {
                callee.binding = MemberBinding::Scope;
                BindNative(call, native->second, qualified);
                return;
            }

            const u32 boundType = BoundTypeNamed(static_cast<const IdentifierExpr&>(*callee.base).name);
            if (boundType != kNoBoundType)
            {
                callee.binding = MemberBinding::Scope;
                BindBoundCall(call, boundType, callee.member, false, qualified);
                return;
            }
        }

        AnalyzeExpr(*callee.base);
        if (IsPoisoned(callee.base->resolvedType))
        {
            call.resolvedType = ValueType::Unknown;
            return;
        }

        // A method reached through a handle runs on whatever the engine
        // says that handle names; the handle itself goes on the stack
        // ahead of the arguments, exactly as a receiver does.
        if (callee.base->resolvedType == ValueType::Handle)
        {
            const u32 boundType = callee.base->resolvedClass;
            call.receiver = std::move(callee.base);
            BindBoundCall(call, boundType, callee.member, true, qualified);
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

    // A method of an engine type. There is no overloading here: a type
    // declares each of its methods once, so the name settles which one is
    // meant and the argument list is checked against it rather than used
    // to choose between candidates.
    void BindBoundCall(CallExpr& call, u32 boundType, const std::string& methodName, bool throughAHandle,
        const std::string& displayName)
    {
        call.target = CallTarget::BoundMethod;
        call.resolvedType = ValueType::Unknown;

        const u32 methodIndex = m_bindings ? FindBoundMethod(*m_bindings, boundType, methodName) : kNoBoundMethod;
        if (methodIndex == kNoBoundMethod)
        {
            Error(call.location, "'" + m_bindings->types[boundType].name + "' has no method named '" + methodName + "'");
            return;
        }

        const BoundMethod& method = m_bindings->types[boundType].methods[methodIndex];

        if (method.isInstance != throughAHandle)
        {
            if (method.isInstance)
                Error(call.location, "'" + displayName + "' runs on an instance, so it cannot be called through the type name");
            else
                Error(call.location, "'" + displayName + "' does not run on an instance, so it cannot be called through one");
            return;
        }

        call.boundType = boundType;
        call.boundMethod = methodIndex;
        call.resolvedType = method.returnType;
        call.resolvedClass = (method.returnType == ValueType::Handle) ? method.returnBoundType : kNoClass;

        if (call.args.size() != method.parameterTypes.size())
        {
            Error(call.location, "method '" + displayName + "' takes " + std::to_string(method.parameterTypes.size()) +
                                     " argument(s) but " + std::to_string(call.args.size()) + " were given");
            return;
        }

        for (size_t i = 0; i < call.args.size(); ++i)
        {
            TypeRef wanted;
            wanted.type = method.parameterTypes[i];
            wanted.classIndex = method.parameterBoundTypes[i];
            CheckAssignable(*call.args[i], wanted, "in argument " + std::to_string(i + 1) + " of '" + displayName + "'");
        }
    }

    // Whether one built-in accepts this argument list exactly as written.
    bool NativeMatchesExactly(const NativeFunctionSignature& signature, const std::vector<ExprPtr>& args) const
    {
        if (signature.parameterCount != (u32)args.size()) return false;
        for (u32 i = 0; i < signature.parameterCount; ++i)
        {
            if (signature.parameterTypes[i] != args[i]->resolvedType) return false;
        }
        return true;
    }

    // The same question allowing the language's one implicit widening,
    // which is the only way an argument may differ from what was
    // declared.
    bool NativeMatchesWidened(const NativeFunctionSignature& signature, const std::vector<ExprPtr>& args) const
    {
        if (signature.parameterCount != (u32)args.size()) return false;
        for (u32 i = 0; i < signature.parameterCount; ++i)
        {
            const ValueType declared = signature.parameterTypes[i];
            const ValueType given = args[i]->resolvedType;
            if (declared == given) continue;
            if (declared == ValueType::Float && given == ValueType::Int) continue;
            return false;
        }
        return true;
    }

    void BindNative(CallExpr& call, const std::vector<NativeFunctionId>& candidates, const std::string& displayName)
    {
        // Every entry sharing a name is a separate accepted argument
        // list, so the whole list is what chooses between them: an exact
        // match first, and only then one reached by widening, so a form
        // written for the argument's own type is never passed over in
        // favour of one that has to convert it.
        call.target = CallTarget::Native;
        call.resolvedType = ValueType::Unknown;

        const NativeFunctionSignature* natives = NativeFunctionTable();

        for (NativeFunctionId candidate : candidates)
        {
            if (!NativeMatchesExactly(natives[(u32)candidate], call.args)) continue;
            call.targetIndex = (u32)candidate;
            call.resolvedType = natives[(u32)candidate].returnType;
            return;
        }

        for (NativeFunctionId candidate : candidates)
        {
            const NativeFunctionSignature& signature = natives[(u32)candidate];
            if (!NativeMatchesWidened(signature, call.args)) continue;

            for (u32 i = 0; i < signature.parameterCount; ++i)
            {
                if (signature.parameterTypes[i] == ValueType::Float && call.args[i]->resolvedType == ValueType::Int)
                    call.args[i]->conversion = ValueType::Float;
            }
            call.targetIndex = (u32)candidate;
            call.resolvedType = signature.returnType;
            return;
        }

        // Nothing accepts this list. An argument that was already
        // reported on says nothing further, so the message is only about
        // a list that is genuinely unmatched.
        for (const ExprPtr& arg : call.args)
        {
            if (IsPoisoned(arg->resolvedType)) return;
        }

        bool countIsRight = false;
        for (NativeFunctionId candidate : candidates)
            countIsRight = countIsRight || natives[(u32)candidate].parameterCount == (u32)call.args.size();

        if (!countIsRight)
        {
            u32 accepted = 0;
            for (NativeFunctionId candidate : candidates) accepted = natives[(u32)candidate].parameterCount;
            Error(call.location, "'" + displayName + "' takes " + std::to_string(accepted) + " argument(s) but " +
                                     std::to_string(call.args.size()) + " were given");
            return;
        }

        std::string written;
        for (size_t i = 0; i < call.args.size(); ++i)
        {
            if (i != 0) written += ", ";
            written += Quoted(*call.args[i]);
        }
        const SourceLocation& blame = call.args.empty() ? call.location : call.args[0]->location;
        Error(blame, "'" + displayName + "' does not accept an argument list of (" + written + ")");
    }
};

} // namespace

bool Analyze(Program& program, DiagnosticList& diagnostics, const BindingTable* bindings)
{
    Analyzer analyzer(program, diagnostics, bindings);
    return analyzer.Run();
}

} // namespace Fluxion::Script
