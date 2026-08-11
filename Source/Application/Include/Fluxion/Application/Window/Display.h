#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionDisplayInfo
{
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    f32 dpiScale; // 1.0 = 96 DPI baseline
    bool primary;
} FluxionDisplayInfo;

u32 Fluxion_Display_GetCount(void);

// Returns false if `index` is out of range.
bool Fluxion_Display_GetInfo(u32 index, FluxionDisplayInfo* outInfo);

#ifdef __cplusplus
}
#endif
