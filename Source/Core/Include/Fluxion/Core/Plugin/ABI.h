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

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raised to 6 when FluxionTypeInfo grew its `methods` span: a reflected
// type now carries callable metadata as well as data members, and a host
// and a plugin that disagree about that disagree about the shape of every
// type they exchange.
#define FLUXION_PLUGIN_HOST_API_VERSION 6u

typedef struct FluxionPluginHostAPI
{
    u32 apiVersion;
    FluxionAllocator* defaultAllocator;

    // Added at version 2. A plugin's Fluxion_Plugin_Load may call this to
    // register a subsystem with the host's Subsystem Registry -- the
    // plugin links against Core headers only, not the Core static lib, so
    // it cannot call Fluxion_SubsystemRegistry_Register directly; this
    // function pointer is how the host exposes that service across the
    // plugin boundary instead.
    bool (*registerSubsystem)(const FluxionSubsystemDesc* desc);

    // Added at version 3, same reasoning as registerSubsystem, now for the
    // Service Registry. If a plugin registers a service, it must
    // unregister it in Fluxion_Plugin_Unload -- the interface pointer
    // lives inside the plugin's own DLL/SO, so it would dangle in the
    // registry once the library is unloaded.
    bool (*registerService)(const void* interfacePointer);
    void (*unregisterService)(FluxionServiceId id);
    const void* (*getService)(FluxionServiceId id, u32 minVersion);

    // Added at version 4, same reasoning again, now for the Reflection
    // Registry. Unlike subsystems/services, reflected types have no
    // per-type unregister (Fluxion_Reflection_Shutdown clears all of
    // them at once) -- a plugin that registers a type must stay loaded
    // for as long as anyone might look that type up.
    bool (*registerType)(const FluxionTypeInfo* typeInfo);
    const FluxionTypeInfo* (*findTypeById)(FluxionTypeId id);

    // Added at version 5. A plugin links against Core headers only, not
    // the Core static lib, so it cannot call Fluxion_Profiler_ZoneBegin/
    // ZoneEnd/Marker directly -- these proxy into whichever profiler
    // backend the host currently has attached (Profiler.h), same
    // reasoning as registerSubsystem/registerService/registerType above.
    void (*profilerZoneBegin)(const FluxionSourceLocation* location, const char* name);
    void (*profilerZoneEnd)(void);
    void (*profilerMarker)(const FluxionSourceLocation* location, const char* name);
} FluxionPluginHostAPI;

typedef struct FluxionPluginAPI
{
    void* userData;
} FluxionPluginAPI;

typedef bool (*FluxionPluginLoadFn)(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi);
typedef void (*FluxionPluginUnloadFn)(FluxionPluginAPI* api);

// Every plugin shared library must export exactly these two C-ABI
// functions (matching FluxionPluginLoadFn/FluxionPluginUnloadFn above),
// e.g.:
//
//   FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi) { ... }
//   FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api) { ... }
#define FLUXION_PLUGIN_LOAD_SYMBOL_NAME   "Fluxion_Plugin_Load"
#define FLUXION_PLUGIN_UNLOAD_SYMBOL_NAME "Fluxion_Plugin_Unload"

#ifdef __cplusplus
}
#endif
