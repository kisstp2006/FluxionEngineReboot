#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// A field that points at an asset.
//
// An id, not a path and not a handle. A path changes when someone renames
// a folder, and in a shipped game there are no paths left to point at; a
// handle means where something sits in a table right now, and means
// nothing once the program that made it has ended.
//
// One consequence is worth saying out loud: a reference like this
// SURVIVES BEING WRITTEN DOWN WITH NO SPECIAL HANDLING AT ALL. It is
// sixteen bytes that mean the same thing in every run. An entity handle
// needed work to save because the opposite is true of it.
typedef struct FluxionAssetRef
{
    // Nil points at nothing -- a field that was never filled in, which is
    // a normal state and not damage.
    FluxionUUID asset;
} FluxionAssetRef;

// Registered so that a field holding one can be RECOGNISED, which is a
// different need from being saved: whoever packages a build has to be
// able to walk an object's fields and ask which assets it will reach for,
// and a field's declared type is how that question gets an answer.
FluxionTypeId Fluxion_AssetRef_TypeId(void);

static inline bool Fluxion_AssetRef_IsSet(FluxionAssetRef ref)
{
    return !Fluxion_UUID_IsNil(ref.asset);
}

#ifdef __cplusplus
}
#endif
