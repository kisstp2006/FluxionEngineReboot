#include "TestFramework.h"

#include <Fluxion/RHI/Handles.h>

void Test_Handles_Run(TestContext* ctx)
{
    FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(invalid));

    FluxionRHIBufferHandle valid = { 0, 0 };
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(valid));

    // Distinct handle types stay distinct nominal C structs -- this is
    // mostly a compile-time property (a FluxionRHIBufferHandle cannot be
    // passed where a FluxionRHITextureHandle is expected), verified here
    // just by confirming both compile and behave identically as
    // index+generation pairs.
    FluxionRHITextureHandle texture = { 0, 0 };
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));
}
