#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

typedef u32 FluxionPropertyFlags;

#define FLUXION_PROPERTY_FLAG_NONE           ((FluxionPropertyFlags)0)
#define FLUXION_PROPERTY_FLAG_READ_ONLY      FLUXION_BIT(0)
#define FLUXION_PROPERTY_FLAG_TRANSIENT      FLUXION_BIT(1) // not serialized
#define FLUXION_PROPERTY_FLAG_EDITOR_VISIBLE FLUXION_BIT(2)
