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

#include "TestFramework.h"

#include <string.h>

#include <Fluxion/Core/Diagnostics/Profiler.h>
#include <Fluxion/Foundation/Memory/MemoryTracker.h>

static int s_zoneBeginCount = 0;
static int s_zoneEndCount = 0;
static int s_markerCount = 0;
static int s_threadNameCount = 0;
static char s_lastName[64];
static void* s_lastUserData = NULL;

// Bounds-safe copy into a fixed buffer, always NUL-terminated -- avoids
// strncpy's "may leave the destination unterminated" footgun without
// pulling in a non-portable *_s variant.
static void Test_CopyName(char* dest, usize destSize, const char* source)
{
    usize length = strlen(source);
    if (length >= destSize) length = destSize - 1;
    memcpy(dest, source, length);
    dest[length] = '\0';
}

static void Test_ZoneBegin(const FluxionSourceLocation* location, const char* name, void* userData)
{
    FLUXION_UNUSED(location);
    ++s_zoneBeginCount;
    s_lastUserData = userData;
    Test_CopyName(s_lastName, sizeof(s_lastName), name);
}

static void Test_ZoneEnd(void* userData)
{
    ++s_zoneEndCount;
    s_lastUserData = userData;
}

static void Test_Marker(const FluxionSourceLocation* location, const char* name, void* userData)
{
    FLUXION_UNUSED(location);
    ++s_markerCount;
    s_lastUserData = userData;
    Test_CopyName(s_lastName, sizeof(s_lastName), name);
}

static void Test_ThreadName(const char* name, void* userData)
{
    ++s_threadNameCount;
    s_lastUserData = userData;
    Test_CopyName(s_lastName, sizeof(s_lastName), name);
}

void Test_Profile_Run(TestContext* ctx)
{
    s_zoneBeginCount = 0;
    s_zoneEndCount = 0;
    s_markerCount = 0;
    s_threadNameCount = 0;

    // With no backend attached, every call is a near-zero-cost no-op --
    // nothing to observe, just confirms it doesn't crash.
    FluxionSourceLocation location;
    location.file = __FILE__;
    location.function = __func__;
    location.line = (u32)__LINE__;
    Fluxion_Profiler_ZoneBegin(&location, "NoBackendZone");
    Fluxion_Profiler_Marker(&location, "NoBackendMarker");
    Fluxion_Profiler_ZoneEnd();
    TEST_CHECK(ctx, s_zoneBeginCount == 0);
    TEST_CHECK(ctx, s_markerCount == 0);
    TEST_CHECK(ctx, s_zoneEndCount == 0);

    int dummyUserData = 0;
    FluxionProfileBackend backend = { 0 };
    backend.zoneBegin = Test_ZoneBegin;
    backend.zoneEnd = Test_ZoneEnd;
    backend.marker = Test_Marker;
    backend.threadName = Test_ThreadName;
    backend.userData = &dummyUserData;
    Fluxion_Profiler_SetBackend(&backend);

    Fluxion_Profiler_ZoneBegin(&location, "TestZone");
    TEST_CHECK(ctx, s_zoneBeginCount == 1);
    TEST_CHECK(ctx, strcmp(s_lastName, "TestZone") == 0);
    TEST_CHECK(ctx, s_lastUserData == &dummyUserData);

    Fluxion_Profiler_Marker(&location, "TestMarker");
    TEST_CHECK(ctx, s_markerCount == 1);
    TEST_CHECK(ctx, strcmp(s_lastName, "TestMarker") == 0);

    Fluxion_Profiler_ZoneEnd();
    TEST_CHECK(ctx, s_zoneEndCount == 1);

    Fluxion_Profiler_SetThreadName("TestThread");
    TEST_CHECK(ctx, s_threadNameCount == 1);
    TEST_CHECK(ctx, strcmp(s_lastName, "TestThread") == 0);

    Fluxion_Profiler_ClearBackend();

    // Cleared -- back to no-op, counts stay where they were.
    Fluxion_Profiler_ZoneBegin(&location, "AfterClear");
    Fluxion_Profiler_ZoneEnd();
    TEST_CHECK(ctx, s_zoneBeginCount == 1);
    TEST_CHECK(ctx, s_zoneEndCount == 1);

    // Attaching a backend also bridges MemoryTracker's domain push/pop
    // into a marker on that same backend (Profiler.c's
    // Fluxion_Profiler_OnMemoryDomainEvent) -- proves an external tool
    // sees memory-scope events through the same adapter as CPU zones,
    // not just CPU zones alone.
    Fluxion_Profiler_SetBackend(&backend);
    s_markerCount = 0;

    Fluxion_MemoryTracker_Init();
    {
        const FluxionMemoryDomainId domain = FLUXION_MEMORY_DOMAIN_ID_OF(TestProfileBridgeDomain);
        FluxionMemoryDomainDesc desc = { domain, "TestProfileBridgeDomain", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        Fluxion_MemoryTracker_RegisterDomain(&desc);

        Fluxion_MemoryTracker_PushDomain(domain, location);
        TEST_CHECK(ctx, s_markerCount == 1);
        TEST_CHECK(ctx, strstr(s_lastName, "TestProfileBridgeDomain") != NULL);
        TEST_CHECK(ctx, strstr(s_lastName, "push") != NULL);

        Fluxion_MemoryTracker_PopDomain();
        TEST_CHECK(ctx, s_markerCount == 2);
        TEST_CHECK(ctx, strstr(s_lastName, "pop") != NULL);
    }
    Fluxion_MemoryTracker_Shutdown();

    Fluxion_Profiler_ClearBackend();
}
