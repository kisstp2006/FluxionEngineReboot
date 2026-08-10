#pragma once

#include <Fluxion/Foundation/Types.h>

// Pure macros below — no declarations, so no extern "C" linkage block is
// needed here (FLUXION_DEFINE_HANDLE expands at each call site instead).

#define FLUXION_HANDLE_INVALID_INDEX 0xFFFFFFFFu

// FLUXION_DEFINE_HANDLE(Name) declares a distinct, strongly-typed
// index+generation handle struct `Name`, so e.g. an AssetHandle and a
// NodeHandle cannot be accidentally interchanged even though both are
// structurally { u32 index; u32 generation; }.
//
// Construct an invalid handle with aggregate initialization:
//   Name handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
#define FLUXION_DEFINE_HANDLE(Name) \
    typedef struct Name \
    { \
        u32 index; \
        u32 generation; \
    } Name

#define FLUXION_HANDLE_IS_VALID(handle) ((handle).index != FLUXION_HANDLE_INVALID_INDEX)
