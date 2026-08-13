#include <Fluxion/Script/Runtime/Binding.hpp>

#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>

#include <string>

namespace Fluxion::Script
{

namespace
{

// The identity the reflection registry gives each of the value types the
// language already has. Computed once rather than at every lookup: the
// identity is a hash of the name, so it costs something to work out and
// never changes afterwards.
struct PrimitiveTypeIds
{
    FluxionTypeId intId;
    FluxionTypeId floatId;
    FluxionTypeId boolId;
    FluxionTypeId stringId;
    FluxionTypeId objectId;
};

const PrimitiveTypeIds& Primitives()
{
    static const PrimitiveTypeIds ids = {
        FLUXION_TYPE_ID_OF(i32),
        FLUXION_TYPE_ID_OF(f32),
        FLUXION_TYPE_ID_OF(bool),
        ScriptStringTypeId(),
        ScriptObjectTypeId(),
    };
    return ids;
}

std::string TextOf(FluxionStringView view)
{
    if (!view.data || view.length == 0) return std::string();
    return std::string(view.data, view.length);
}

// What one reflected type identity means on the script side. A type the
// table already holds becomes a handle to that type; everything else has
// to be one of the value types the language has, or the method carrying
// it cannot be called from a script at all.
bool TranslateTypeId(const BindingTable& table, FluxionTypeId id, ValueType& outType, u32& outBoundType)
{
    outBoundType = kNoBoundType;

    if (id == FLUXION_TYPE_ID_INVALID)
    {
        outType = ValueType::Void;
        return true;
    }

    const PrimitiveTypeIds& primitives = Primitives();
    if (id == primitives.intId) { outType = ValueType::Int; return true; }
    if (id == primitives.floatId) { outType = ValueType::Float; return true; }
    if (id == primitives.boolId) { outType = ValueType::Bool; return true; }
    if (id == primitives.stringId) { outType = ValueType::String; return true; }
    if (id == primitives.objectId) { outType = ValueType::Object; return true; }

    const u32 bound = FindBoundTypeById(table, id);
    if (bound != kNoBoundType)
    {
        outType = ValueType::Handle;
        outBoundType = bound;
        return true;
    }

    outType = ValueType::Unknown;
    return false;
}

} // namespace

u32 FindBoundType(const BindingTable& table, const std::string& name)
{
    for (u32 i = 0; i < (u32)table.types.size(); ++i)
    {
        if (table.types[i].name == name) return i;
    }
    return kNoBoundType;
}

u32 FindBoundTypeById(const BindingTable& table, FluxionTypeId id)
{
    if (id == FLUXION_TYPE_ID_INVALID) return kNoBoundType;
    for (u32 i = 0; i < (u32)table.types.size(); ++i)
    {
        if (table.types[i].typeId == id) return i;
    }
    return kNoBoundType;
}

u32 FindBoundMethod(const BindingTable& table, u32 typeIndex, const std::string& name)
{
    if (typeIndex >= table.types.size()) return kNoBoundMethod;

    const BoundType& type = table.types[typeIndex];
    for (u32 i = 0; i < (u32)type.methods.size(); ++i)
    {
        if (type.methods[i].name == name) return i;
    }
    return kNoBoundMethod;
}

u32 AddBoundType(BindingTable& table, const FluxionTypeInfo& typeInfo, HandleResolverFn resolve, void* resolveUser,
    DiagnosticList& outDiagnostics)
{
    const std::string name = TextOf(typeInfo.name);
    const SourceLocation location{ name, 0, 0 };

    if (name.empty())
    {
        outDiagnostics.AddError(location, "an engine type made visible to scripts must have a name");
        return kNoBoundType;
    }
    if (FindBoundType(table, name) != kNoBoundType)
    {
        outDiagnostics.AddError(location, "the engine type '" + name + "' is already visible to scripts");
        return kNoBoundType;
    }

    // The entry goes in before its methods are read, so a method of this
    // type may take or answer with a handle to this same type.
    const u32 index = (u32)table.types.size();
    table.types.emplace_back();

    BoundType built;
    built.name = name;
    built.typeId = typeInfo.id;
    built.resolve = resolve;
    built.resolveUser = resolveUser;
    table.types[index] = built;

    bool ok = true;
    for (usize m = 0; m < typeInfo.methods.count; ++m)
    {
        const auto* declared = (const FluxionMethodInfo*)Fluxion_Span_At(typeInfo.methods, m);

        BoundMethod method;
        method.name = TextOf(declared->name);
        method.isInstance = (declared->flags & FLUXION_METHOD_FLAG_STATIC) == 0;
        method.takesScriptType = (declared->flags & FLUXION_METHOD_FLAG_SCRIPT_TYPE_ARGUMENT) != 0;
        method.invoke = declared->invoke;

        if (method.name.empty())
        {
            outDiagnostics.AddError(location, "'" + name + "' declares a method with no name");
            ok = false;
            break;
        }
        if (!method.invoke)
        {
            outDiagnostics.AddError(location, "'" + name + "." + method.name + "' has no way to be called");
            ok = false;
            break;
        }
        if (method.isInstance && !resolve)
        {
            outDiagnostics.AddError(location, "'" + name + "." + method.name +
                                                  "' runs on an instance, so '" + name + "' needs a way to turn a handle into one");
            ok = false;
            break;
        }

        if (!TranslateTypeId(table, declared->returnType, method.returnType, method.returnBoundType))
        {
            outDiagnostics.AddError(location, "'" + name + "." + method.name + "' answers with a type a script cannot hold");
            ok = false;
            break;
        }

        for (u32 p = 0; p < declared->parameterCount && ok; ++p)
        {
            ValueType parameterType = ValueType::Unknown;
            u32 parameterBound = kNoBoundType;
            if (!TranslateTypeId(table, declared->parameterTypes[p], parameterType, parameterBound) ||
                parameterType == ValueType::Void)
            {
                outDiagnostics.AddError(location, "argument " + std::to_string(p + 1) + " of '" + name + "." + method.name +
                                                      "' has a type a script cannot pass");
                ok = false;
                break;
            }
            method.parameterTypes.push_back(parameterType);
            method.parameterBoundTypes.push_back(parameterBound);

            // One entry per parameter, so the three lists can always be
            // read together. Which of them, if any, takes a constant of a
            // named set is said afterwards, by the host.
            method.parameterConstantSets.emplace_back();
        }
        if (!ok) break;

        // The type argument arrives as a class index, so the parameter it
        // lands in has to be one that can hold one.
        if (method.takesScriptType && (method.parameterTypes.empty() || method.parameterTypes[0] != ValueType::Int))
        {
            outDiagnostics.AddError(location, "'" + name + "." + method.name +
                                                  "' is written to be called with a type, so its first argument must be an int");
            ok = false;
            break;
        }

        if (FindBoundMethod(table, index, method.name) != kNoBoundMethod)
        {
            outDiagnostics.AddError(location, "'" + name + "' declares more than one method named '" + method.name + "'");
            ok = false;
            break;
        }

        table.types[index].methods.push_back(std::move(method));
    }

    // A type is either wholly visible or not visible at all: half of one
    // would let a script call some of what it declared and be told the
    // rest does not exist.
    if (!ok)
    {
        table.types.pop_back();
        return kNoBoundType;
    }
    return index;
}

bool BindParameterToConstantSet(BindingTable& table, u32 typeIndex, const std::string& methodName, u32 parameterIndex,
    const std::string& setName)
{
    if (typeIndex >= table.types.size() || setName.empty()) return false;

    const u32 methodIndex = FindBoundMethod(table, typeIndex, methodName);
    if (methodIndex == kNoBoundMethod) return false;

    BoundMethod& method = table.types[typeIndex].methods[methodIndex];
    if (parameterIndex >= method.parameterTypes.size()) return false;

    // A constant travels as the number it stands for, so the parameter it
    // lands in has to be one that holds a whole number.
    if (method.parameterTypes[parameterIndex] != ValueType::Int) return false;

    method.parameterConstantSets.resize(method.parameterTypes.size());
    method.parameterConstantSets[parameterIndex] = setName;
    return true;
}

} // namespace Fluxion::Script
