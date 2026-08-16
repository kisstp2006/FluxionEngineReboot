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
