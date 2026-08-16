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

#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Result.h>

static FluxionResult PluginSubsystem_Startup(void* userdata)
{
    FLUXION_UNUSED(userdata);
    return Fluxion_ResultOk();
}

static void PluginSubsystem_Shutdown(void* userdata)
{
    FLUXION_UNUSED(userdata);
}

typedef struct PluginTestServiceV1
{
    FluxionServiceHeader header;
    i32 magicValue;
} PluginTestServiceV1;

// The interface pointer handed to Fluxion_ServiceRegistry_Register must
// stay alive for as long as it's registered -- static storage, not a
// stack local, since it outlives the Fluxion_Plugin_Load call.
static PluginTestServiceV1 s_pluginService;

typedef struct PluginTestReflectedType
{
    f32 value;
} PluginTestReflectedType;

// Same lifetime requirement as s_pluginService -- the Reflection Registry
// only stores the pointer (Registry.h), and has no per-type unregister,
// so this must outlive the whole time the plugin stays loaded.
static FluxionPropertyInfo s_pluginTypeProperties[1];
static FluxionTypeInfo s_pluginTypeInfo;

// Fluxion_Plugin_Unload doesn't receive the host pointer (see ABI.h), so
// Load stashes it here for Unload to reach unregisterService -- a real
// plugin caching the host interface across its own lifetime, not just a
// test convenience.
static const FluxionPluginHostAPI* s_host = NULL;

// Proves a plugin can register a subsystem, a service, and a reflected
// type with the host's registries across the plugin boundary (via
// FluxionPluginHostAPI), not just call APIs the host already knew about
// at build time.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    outApi->userData = NULL;
    s_host = host;

    if (!host->registerSubsystem || !host->registerService || !host->registerType ||
        !host->profilerZoneBegin || !host->profilerZoneEnd || !host->profilerMarker)
    {
        return false;
    }

    FluxionSubsystemDesc subsystemDesc;
    subsystemDesc.id = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
    subsystemDesc.name = "PluginSubsystem";
    subsystemDesc.phase = FLUXION_STARTUP_PHASE_RUNTIME;
    subsystemDesc.dependencies = NULL;
    subsystemDesc.dependencyCount = 0;
    subsystemDesc.startup = PluginSubsystem_Startup;
    subsystemDesc.shutdown = PluginSubsystem_Shutdown;
    subsystemDesc.userdata = NULL;

    if (!host->registerSubsystem(&subsystemDesc))
    {
        return false;
    }

    s_pluginService.header.serviceId = FLUXION_SERVICE_ID_OF(PluginTestService);
    s_pluginService.header.version = 1;
    s_pluginService.header.structSize = sizeof(s_pluginService);
    s_pluginService.magicValue = 42;

    if (!host->registerService(&s_pluginService))
    {
        return false;
    }

    // Proves a plugin can drive the host's profiler across the DLL
    // boundary too (via profilerZoneBegin/profilerMarker/profilerZoneEnd
    // in the Host API), not just the registries above -- wraps the type
    // registration below in a zone with a marker in the middle.
    FluxionSourceLocation location;
    location.file = __FILE__;
    location.function = __func__;
    location.line = (u32)__LINE__;
    host->profilerZoneBegin(&location, "PluginSubsystemPlugin.RegisterReflectedType");

    // FLUXION_TYPE_ID_OF hashes at runtime, so this can't be a file-scope
    // static initializer -- assigned here instead, as a C99 compound
    // literal (this file is always compiled as C, so unlike the other
    // FLUXION_REFLECT_PROPERTY call sites this doesn't need to also stay
    // valid C++ syntax) into static storage, since the Reflection
    // Registry only stores the pointer and must still be able to read it
    // long after Fluxion_Plugin_Load returns.
    s_pluginTypeProperties[0] = (FluxionPropertyInfo)FLUXION_REFLECT_PROPERTY(PluginTestReflectedType, value, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE);

    s_pluginTypeInfo.name = Fluxion_StringView_FromCStr("PluginTestReflectedType");
    s_pluginTypeInfo.id = FLUXION_TYPE_ID_OF(PluginTestReflectedType);
    s_pluginTypeInfo.kind = FLUXION_TYPE_KIND_STRUCT;
    s_pluginTypeInfo.size = sizeof(PluginTestReflectedType);
    s_pluginTypeInfo.version = 1;
    s_pluginTypeInfo.members = Fluxion_Span_Make(s_pluginTypeProperties, FLUXION_ARRAY_COUNT(s_pluginTypeProperties), sizeof(FluxionPropertyInfo));

    bool typeRegistered = host->registerType(&s_pluginTypeInfo);

    host->profilerMarker(&location, "PluginSubsystemPlugin.TypeRegistered");
    host->profilerZoneEnd();

    return typeRegistered;
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);

    // Must unregister before unload finishes -- s_pluginService lives in
    // this DLL/SO's own memory, so it would dangle in the host's registry
    // the moment the library is unmapped. The reflected type has no
    // per-type unregister (see ABI.h) -- the test that loads this plugin
    // only looks types up while it's still loaded.
    if (s_host && s_host->unregisterService)
    {
        s_host->unregisterService(FLUXION_SERVICE_ID_OF(PluginTestService));
    }
}
