#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Every property this serializer handles must fit in this many bytes --
// covers every primitive and every Math.h type (FluxionMat4, the
// largest, is exactly 64 bytes). A property larger than this is a
// programming error (FLUXION_ASSERT), not a data problem.
#define FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE 64

// How long a text property may be. Text is read into a buffer of its own
// on the way in, because the setter is handed a pointer rather than the
// characters; anything longer is stepped over rather than truncated.
#define FLUXION_BINARY_SERIALIZER_MAX_TEXT_LENGTH 256

// Serializes (stream in write mode) or deserializes (read mode)
// `instance`, an object of the reflected type `typeInfo`, to/from
// `stream`. Writes typeInfo->id + typeInfo->version once, then one tag
// per property: a name hash, a byte size, and the raw value (offset- or
// accessor-mode property access, whichever typeInfo says).
//
// On read: a stored property tag whose name hash isn't found in
// typeInfo->members (or whose size no longer matches) is skipped by its
// recorded size, without touching `instance` -- so instance keeps
// whatever value it already had for that property (its default). A
// property that IS declared in typeInfo but has no matching tag in the
// stream is simply never written to, for the same reason. This
// mechanism is what lets an old stream load into a struct with new
// properties added, and a newer stream partially load into an older
// struct, without either side needing to agree on the exact same
// property set. Returns false if the stored type id doesn't match
// typeInfo->id, or if the stream overflowed.
bool Fluxion_BinarySerializer_Serialize(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance);

// The same, leaving every property whose type is in `skipTypes` entirely
// alone -- not written, not looked for on the way back in.
//
// For a value this cannot carry across on its own. A field holding a
// handle is the case that matters: what a handle names is decided by
// whatever handed it out, and its bytes mean nothing anywhere else. The
// caller who knows what such a field means writes it in terms that
// survive, and says so here.
//
// This layer is deliberately not told WHY a type is skipped, only that it
// is: knowing would mean knowing about things built on top of it.
bool Fluxion_BinarySerializer_SerializeExcept(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance,
                                              const FluxionTypeId* skipTypes, u32 skipCount);

#ifdef __cplusplus
}
#endif
