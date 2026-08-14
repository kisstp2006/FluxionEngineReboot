#pragma once

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Fluxion::Core
{

// A reflectable C++ type exposes its own name as a static member -- same
// convention as SubsystemType/ServiceType. FLUXION_TYPE_ID_OF can't be
// used here since NO_RTTI rules out typeid(T).name() as a name source.
template<typename T>
concept ReflectableType = requires
{
    { T::Name } -> std::convertible_to<const char*>;
};

// The id a reflectable type is known by, worked out from the same name it
// declares. Anything that has to name a type across the C interface --
// which takes ids, not types -- goes through here rather than hashing the
// name itself, so there is one place the two can agree or disagree.
template<ReflectableType T>
FluxionTypeId TypeIdOf()
{
    return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(T::Name));
}

template<ReflectableType T>
const FluxionTypeInfo* GetTypeInfo()
{
    return Fluxion_Reflection_FindTypeById(TypeIdOf<T>());
}

// Reads a property's value out of `instance` into `outValue`, dispatching
// on the property's access kind -- callers don't need to know whether a
// given property is offset- or accessor-based.
template<typename T, typename ValueT>
void GetPropertyValue(const T& instance, const FluxionPropertyInfo& property, ValueT& outValue)
{
    const void* base = &instance;
    if (property.accessKind == FLUXION_PROPERTY_ACCESS_OFFSET)
    {
        std::memcpy(&outValue, static_cast<const u8*>(base) + property.offset, sizeof(ValueT));
    }
    else
    {
        property.accessor.getter(base, &outValue);
    }
}

template<typename T, typename ValueT>
void SetPropertyValue(T& instance, const FluxionPropertyInfo& property, const ValueT& value)
{
    void* base = &instance;
    if (property.accessKind == FLUXION_PROPERTY_ACCESS_OFFSET)
    {
        std::memcpy(static_cast<u8*>(base) + property.offset, &value, sizeof(ValueT));
    }
    else
    {
        property.accessor.setter(base, &value);
    }
}

namespace Detail
{
    template<typename>
    struct GetterTraits;

    template<typename C, typename R>
    struct GetterTraits<R (C::*)() const>
    {
        using ClassType = C;
        using ValueType = std::remove_cvref_t<R>;
    };

    template<typename>
    struct SetterTraits;

    template<typename C, typename R>
    struct SetterTraits<void (C::*)(R)>
    {
        using ClassType = C;
        using ValueType = std::remove_cvref_t<R>;
    };

    // Bridges a real C++ getter/setter member function pair to the C
    // function-pointer shape FluxionPropertyInfo's accessor mode expects
    // -- the same trampoline idiom Subsystem.hpp/Service.hpp already use
    // for their own C++-to-C-ABI boundaries.
    template<auto Getter, auto Setter>
    struct AccessorTrampoline
    {
        using ClassType = typename GetterTraits<decltype(Getter)>::ClassType;
        using ValueType = typename GetterTraits<decltype(Getter)>::ValueType;

        static_assert(std::is_same_v<ClassType, typename SetterTraits<decltype(Setter)>::ClassType>,
            "getter and setter must belong to the same class");
        static_assert(std::is_same_v<ValueType, typename SetterTraits<decltype(Setter)>::ValueType>,
            "getter and setter must agree on the property's value type");

        static void Get(const void* instance, void* outValue)
        {
            const ClassType* self = static_cast<const ClassType*>(instance);
            *static_cast<ValueType*>(outValue) = (self->*Getter)();
        }

        static void Set(void* instance, const void* value)
        {
            ClassType* self = static_cast<ClassType*>(instance);
            (self->*Setter)(*static_cast<const ValueType*>(value));
        }
    };

    // Bridges a real C++ function or member function to the single C
    // function-pointer shape FluxionMethodInfo::invoke expects, so a
    // binding author never writes the `void**` unpacking by hand. Same
    // trampoline idiom as AccessorTrampoline above; the second template
    // parameter exists only to let the signature be pattern-matched out
    // of the pointer's own type.
    //
    // Both packs below are expanded together: `A` names the declared
    // parameter types and `I` their positions, so `args[I]` is read back
    // as exactly the type the parameter was declared with.
    template<auto Method, typename Signature = decltype(Method)>
    struct MethodTrampoline;

    template<auto Method, typename R, typename... A>
    struct MethodTrampoline<Method, R (*)(A...)>
    {
        using ReturnType = std::remove_cvref_t<R>;
        static constexpr bool IsStatic = true;
        static constexpr u32 ParameterCount = (u32)sizeof...(A);

        static void Invoke(void* instance, void** args, void* returnValue)
        {
            (void)instance;
            (void)args;
            (void)returnValue;
            Call(args, returnValue, std::index_sequence_for<A...>{});
        }

    private:
        template<usize... I>
        static void Call(void** args, void* returnValue, std::index_sequence<I...>)
        {
            (void)args;
            if constexpr (std::is_void_v<R>)
            {
                (void)returnValue;
                Method(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
            else
            {
                *static_cast<ReturnType*>(returnValue) = Method(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
        }
    };

    template<auto Method, typename R, typename C, typename... A>
    struct MethodTrampoline<Method, R (C::*)(A...)>
    {
        using ClassType = C;
        using ReturnType = std::remove_cvref_t<R>;
        static constexpr bool IsStatic = false;
        static constexpr u32 ParameterCount = (u32)sizeof...(A);

        static void Invoke(void* instance, void** args, void* returnValue)
        {
            (void)args;
            (void)returnValue;
            Call(static_cast<C*>(instance), args, returnValue, std::index_sequence_for<A...>{});
        }

    private:
        template<usize... I>
        static void Call(C* self, void** args, void* returnValue, std::index_sequence<I...>)
        {
            (void)args;
            if constexpr (std::is_void_v<R>)
            {
                (void)returnValue;
                (self->*Method)(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
            else
            {
                *static_cast<ReturnType*>(returnValue) = (self->*Method)(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
        }
    };

    template<auto Method, typename R, typename C, typename... A>
    struct MethodTrampoline<Method, R (C::*)(A...) const>
    {
        using ClassType = C;
        using ReturnType = std::remove_cvref_t<R>;
        static constexpr bool IsStatic = false;
        static constexpr u32 ParameterCount = (u32)sizeof...(A);

        static void Invoke(void* instance, void** args, void* returnValue)
        {
            (void)args;
            (void)returnValue;
            Call(static_cast<const C*>(instance), args, returnValue, std::index_sequence_for<A...>{});
        }

    private:
        template<usize... I>
        static void Call(const C* self, void** args, void* returnValue, std::index_sequence<I...>)
        {
            (void)args;
            if constexpr (std::is_void_v<R>)
            {
                (void)returnValue;
                (self->*Method)(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
            else
            {
                *static_cast<ReturnType*>(returnValue) = (self->*Method)(*static_cast<std::remove_cvref_t<A>*>(args[I])...);
            }
        }
    };
}

// How many parameters a reflected method takes, and whether it needs an
// instance -- both read straight off the pointer, so a descriptor can
// never disagree with the function it names.
template<auto Method>
inline constexpr u32 MethodParameterCount = Detail::MethodTrampoline<Method>::ParameterCount;

template<auto Method>
inline constexpr bool MethodIsStatic = Detail::MethodTrampoline<Method>::IsStatic;

// Builds a FluxionMethodInfo from a plain function or member function
// pointer, e.g.
// ReflectMethod<&Counter::Add>("Add", FLUXION_TYPE_ID_OF(i32), parameterTypes).
// `parameterTypes` must have one entry per declared parameter -- the
// count is checked here rather than trusted -- and the caller keeps it
// alive for as long as the descriptor is used, exactly as it does for the
// property array behind FluxionTypeInfo::members.
template<auto Method, usize N>
FluxionMethodInfo ReflectMethod(const char* name, FluxionTypeId returnType, const FluxionTypeId (&parameterTypes)[N])
{
    using Trampoline = Detail::MethodTrampoline<Method>;
    static_assert(N == Trampoline::ParameterCount, "the parameter type list must have one entry per declared parameter");

    FluxionMethodInfo info{};
    info.name = Fluxion_StringView_FromCStr(name);
    info.returnType = returnType;
    info.parameterTypes = parameterTypes;
    info.parameterCount = (u32)N;
    info.flags = Trampoline::IsStatic ? FLUXION_METHOD_FLAG_STATIC : FLUXION_METHOD_FLAG_NONE;
    info.invoke = &Trampoline::Invoke;
    return info;
}

// The same, for a method that takes nothing at all.
template<auto Method>
FluxionMethodInfo ReflectMethod(const char* name, FluxionTypeId returnType)
{
    using Trampoline = Detail::MethodTrampoline<Method>;
    static_assert(Trampoline::ParameterCount == 0, "this method takes parameters, so it needs a parameter type list");

    FluxionMethodInfo info{};
    info.name = Fluxion_StringView_FromCStr(name);
    info.returnType = returnType;
    info.parameterTypes = nullptr;
    info.parameterCount = 0;
    info.flags = Trampoline::IsStatic ? FLUXION_METHOD_FLAG_STATIC : FLUXION_METHOD_FLAG_NONE;
    info.invoke = &Trampoline::Invoke;
    return info;
}

// Calls a reflected method the way FluxionMethodInfo::invoke expects,
// without the caller having to assemble the pointer array by hand.
inline void InvokeMethod(const FluxionMethodInfo& method, void* instance, void** args, void* returnValue)
{
    if (!method.invoke) return;
    method.invoke(instance, args, returnValue);
}

// Builds an accessor-mode FluxionPropertyInfo from a real getter/setter
// member function pair, e.g.
// ReflectAccessor<&Material::GetRoughness, &Material::SetRoughness>(
//     "roughness", FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE).
// Getter must be `ValueT (Class::*)() const`, Setter `void (Class::*)(ValueT)`.
template<auto Getter, auto Setter>
FluxionPropertyInfo ReflectAccessor(const char* name, FluxionTypeId typeId, FluxionPropertyFlags flags)
{
    using Trampoline = Detail::AccessorTrampoline<Getter, Setter>;

    FluxionPropertyInfo info{};
    info.name = Fluxion_StringView_FromCStr(name);
    info.type = typeId;
    info.size = sizeof(typename Trampoline::ValueType);
    info.flags = flags;
    info.accessKind = FLUXION_PROPERTY_ACCESS_ACCESSOR;
    info.accessor.getter = &Trampoline::Get;
    info.accessor.setter = &Trampoline::Set;
    return info;
}

} // namespace Fluxion::Core
