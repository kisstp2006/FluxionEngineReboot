#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionAssetTypeId, AssetTypeId)

#define FLUXION_ASSET_TYPE_ID_INVALID ((FluxionAssetTypeId)0)

#ifdef __cplusplus
}
#endif

// An asset type is named, and the name is what a build setting writes
// down when it says which types ship. So the id has to come from the name
// and from nothing else: an id derived from registration order, or from a
// pointer, would move when an unrelated plugin loaded, and a setting
// written last week would then mean something different this week.
//
// Separate from FluxionTypeId on purpose. A reflected struct and an asset
// type are different things -- most asset types have no reflected struct
// at all -- and sharing one namespace would let two unrelated names
// collide with each other for no gain.
#define FLUXION_ASSET_TYPE_ID_OF(Name) Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(#Name))
