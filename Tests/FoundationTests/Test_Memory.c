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
