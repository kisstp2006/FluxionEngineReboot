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

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Memory/Arena.h>
#include <Fluxion/Foundation/Memory/ScratchAllocator.h>

#include <string.h>

void Test_Memory_Run(TestContext* ctx)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    TEST_CHECK(ctx, allocator != NULL);

    void* block = Fluxion_Allocator_Alloc(allocator, 128, 16);
    TEST_CHECK(ctx, block != NULL);
    TEST_CHECK(ctx, ((usize)block % 16) == 0);
    memset(block, 0xAB, 128);
    Fluxion_Allocator_Free(allocator, block, 128);

    FluxionArena arena;
    Fluxion_Arena_Init(&arena, allocator, 256);

    void* a = Fluxion_Arena_Alloc(&arena, 64, 8);
    void* b = Fluxion_Arena_Alloc(&arena, 64, 8);
    TEST_CHECK(ctx, a != NULL);
    TEST_CHECK(ctx, b != NULL);
    TEST_CHECK(ctx, a != b);

    void* tooBig = Fluxion_Arena_Alloc(&arena, 1024, 8);
    TEST_CHECK(ctx, tooBig == NULL);

    Fluxion_Arena_Reset(&arena);
    void* afterReset = Fluxion_Arena_Alloc(&arena, 200, 8);
    TEST_CHECK(ctx, afterReset != NULL);

    Fluxion_Arena_Destroy(&arena);

    FluxionScratchAllocator scratch;
    Fluxion_ScratchAllocator_Init(&scratch, allocator, 128);
    void* scratchBlock = Fluxion_ScratchAllocator_Alloc(&scratch, 32, 8);
    TEST_CHECK(ctx, scratchBlock != NULL);
    Fluxion_ScratchAllocator_Reset(&scratch);
    Fluxion_ScratchAllocator_Destroy(&scratch);
}
