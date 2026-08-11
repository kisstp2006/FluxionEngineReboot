#pragma once

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <cstring>
#include <type_traits>

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

template<ReflectableType T>
const FluxionTypeInfo* GetTypeInfo()
{
    const FluxionTypeId id = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(T::Name));
    return Fluxion_Reflection_FindTypeById(id);
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
