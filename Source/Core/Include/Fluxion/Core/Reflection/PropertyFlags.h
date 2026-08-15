#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

typedef u32 FluxionPropertyFlags;

#define FLUXION_PROPERTY_FLAG_NONE           ((FluxionPropertyFlags)0)
#define FLUXION_PROPERTY_FLAG_READ_ONLY      FLUXION_BIT(0)
#define FLUXION_PROPERTY_FLAG_TRANSIENT      FLUXION_BIT(1) // not serialized
#define FLUXION_PROPERTY_FLAG_EDITOR_VISIBLE FLUXION_BIT(2)

// The value is a pointer to null-terminated characters rather than the
// characters themselves.
//
// Without this a serializer writing the property's bytes would write the
// POINTER -- eight bytes naming an address in the process that wrote it,
// which means nothing to the one that reads it. Marked so, the text
// itself is written instead, as a length and that many bytes.
//
// It says nothing about who owns the characters. On the way out they are
// read and copied at once; on the way in the setter is handed a pointer
// good for that call, which is the same rule every other transient string
// in this engine already follows.
#define FLUXION_PROPERTY_FLAG_TEXT           FLUXION_BIT(3)
