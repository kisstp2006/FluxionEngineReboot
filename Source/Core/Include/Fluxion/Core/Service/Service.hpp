#pragma once

#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace Fluxion::Core
{

// A service interface is any standard-layout type exposing `Name`/
// `Version` as static members and embedding a FluxionServiceHeader (named
// `header`) as its first member -- RegisterService fills that header in
// from T::Name/T::Version/sizeof(T); the offset-0 requirement is what
// lets Fluxion_ServiceRegistry_Register read it back out of a bare
// `const void*` on the C side.
template<typename T>
concept ServiceType = requires
{
    { T::Name } -> std::convertible_to<const char*>;
    { T::Version } -> std::convertible_to<u32>;
} && std::is_standard_layout_v<T>;

template<ServiceType T>
bool RegisterService(T& interfaceInstance)
{
    static_assert(offsetof(T, header) == 0, "service interface must embed FluxionServiceHeader as its first member, named `header`");

    interfaceInstance.header.serviceId = Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name));
    interfaceInstance.header.version = T::Version;
    interfaceInstance.header.structSize = sizeof(T);
    return Fluxion_ServiceRegistry_Register(&interfaceInstance);
}

template<ServiceType T>
void UnregisterService()
{
    Fluxion_ServiceRegistry_Unregister(Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name)));
}

template<ServiceType T>
const T* GetService()
{
    const FluxionServiceId id = Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name));
    return static_cast<const T*>(Fluxion_ServiceRegistry_Get(id, T::Version));
}

} // namespace Fluxion::Core
