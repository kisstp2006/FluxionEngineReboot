#pragma once

#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>
#include <Fluxion/Foundation/Result.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <span>

namespace Fluxion::Core
{

// A subsystem is any type exposing this shape as static members -- no base
// class, no virtual dispatch, just a compile-time shape check. `Name`
// becomes the subsystem's stable FluxionSubsystemId via the same hashing
// FLUXION_SUBSYSTEM_ID_OF uses on the C side.
template<typename T>
concept SubsystemType = requires
{
    { T::Name } -> std::convertible_to<const char*>;
    { T::Startup() } -> std::same_as<FluxionResult>;
    { T::Shutdown() } -> std::same_as<void>;
};

namespace Detail
{
    template<SubsystemType T>
    struct SubsystemTrampoline
    {
        static FluxionResult Startup(void*) { return T::Startup(); }
        static void Shutdown(void*) { T::Shutdown(); }
    };
}

// Builds a FluxionSubsystemDesc from T and hands it to the C registry.
// Must be called explicitly (e.g. from an engine bootstrap function) --
// never from a namespace-scope static object's constructor, which would
// reintroduce the static initialization order problems the C kernel is
// specifically designed to avoid.
template<SubsystemType T>
bool RegisterSubsystem(FluxionStartupPhase phase, std::span<const FluxionSubsystemId> dependencies = {})
{
    FluxionSubsystemDesc desc{};
    desc.id = Fluxion_SubsystemId_FromName(Fluxion_StringView_FromCStr(T::Name));
    desc.name = T::Name;
    desc.phase = phase;
    desc.dependencies = dependencies.data();
    desc.dependencyCount = static_cast<u32>(dependencies.size());
    desc.startup = &Detail::SubsystemTrampoline<T>::Startup;
    desc.shutdown = &Detail::SubsystemTrampoline<T>::Shutdown;
    desc.userdata = nullptr;
    return Fluxion_SubsystemRegistry_Register(&desc);
}

} // namespace Fluxion::Core
