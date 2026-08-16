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

#include <Fluxion/Platform/Thread.h>

#include <Fluxion/Foundation/Memory/Allocator.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct FluxionThreadTrampolineData
{
    FluxionThreadFn fn;
    void* userData;
} FluxionThreadTrampolineData;

static DWORD WINAPI Fluxion_ThreadTrampoline(LPVOID parameter)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)parameter;
    data->fn(data->userData);
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
    return 0;
}

static void Fluxion_SetThreadDescriptionUtf8(HANDLE handle, const char* name)
{
    wchar_t wideName[128];
    int converted = MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName, 128);
    if (converted > 0)
    {
        SetThreadDescription(handle, wideName);
    }
}

bool Fluxion_Platform_ThreadCreate(FluxionThread* thread, FluxionThreadFn fn, void* userData, const char* name)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)Fluxion_Allocator_Alloc(
        Fluxion_DefaultAllocator(), sizeof(FluxionThreadTrampolineData), sizeof(void*));
    data->fn = fn;
    data->userData = userData;

    HANDLE handle = CreateThread(NULL, 0, Fluxion_ThreadTrampoline, data, 0, NULL);
    if (!handle)
    {
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
        thread->handle = NULL;
        return false;
    }

    thread->handle = (void*)handle;

    if (name)
    {
        Fluxion_SetThreadDescriptionUtf8(handle, name);
    }

    return true;
}

void Fluxion_Platform_ThreadJoin(FluxionThread* thread)
{
    if (thread->handle)
    {
        WaitForSingleObject((HANDLE)thread->handle, INFINITE);
        CloseHandle((HANDLE)thread->handle);
        thread->handle = NULL;
    }
}

void Fluxion_Platform_ThreadDetach(FluxionThread* thread)
{
    if (thread->handle)
    {
        CloseHandle((HANDLE)thread->handle);
        thread->handle = NULL;
    }
}

void Fluxion_Platform_SetCurrentThreadName(const char* name)
{
    Fluxion_SetThreadDescriptionUtf8(GetCurrentThread(), name);
}

u64 Fluxion_Platform_GetCurrentThreadId(void)
{
    return (u64)GetCurrentThreadId();
}
