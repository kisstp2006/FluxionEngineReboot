#pragma once

// C++-only companion to ABI.h -- never included by C code (the plugin ABI
// itself stays plain C, see ABI.h). Every type that crosses the
// plugin/DLL boundary is asserted standard-layout + trivially copyable
// here: both properties are exactly what "safe to read/write byte-for-
// byte across a C ABI boundary" means in C++ terms, and a static_assert
// catches a future accidental STL member/virtual/non-trivial special
// member at compile time, for free, before it ever reaches a plugin.

#include <Fluxion/Core/Diagnostics/ProfileBackend.h>
#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Core/Plugin/Descriptor.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <type_traits>

namespace Fluxion::Core::Detail
{

template<typename T>
constexpr bool IsAbiSafe = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(IsAbiSafe<FluxionPluginHostAPI>, "FluxionPluginHostAPI must stay ABI-safe -- it crosses the plugin boundary by value pointer");
static_assert(IsAbiSafe<FluxionPluginAPI>, "FluxionPluginAPI must stay ABI-safe -- it crosses the plugin boundary by value pointer");
static_assert(IsAbiSafe<FluxionPluginDescriptor>, "FluxionPluginDescriptor must stay ABI-safe -- read from a .plugin file before the plugin DLL/SO is even loaded");
static_assert(IsAbiSafe<FluxionSubsystemDesc>, "FluxionSubsystemDesc must stay ABI-safe -- registered across the plugin boundary via FluxionPluginHostAPI::registerSubsystem");
static_assert(IsAbiSafe<FluxionTypeInfo>, "FluxionTypeInfo must stay ABI-safe -- registered across the plugin boundary via FluxionPluginHostAPI::registerType");
static_assert(IsAbiSafe<FluxionServiceHeader>, "FluxionServiceHeader must stay ABI-safe -- every service interface struct embeds this as its first member at offset 0");
static_assert(IsAbiSafe<FluxionAllocator>, "FluxionAllocator must stay ABI-safe -- handed across the plugin boundary via FluxionPluginHostAPI::defaultAllocator");
static_assert(IsAbiSafe<FluxionProfileBackend>, "FluxionProfileBackend must stay ABI-safe -- the profiler adapter table an external tool attaches");
static_assert(IsAbiSafe<FluxionSourceLocation>, "FluxionSourceLocation must stay ABI-safe -- passed by pointer into the profiler Host API functions");

} // namespace Fluxion::Core::Detail
