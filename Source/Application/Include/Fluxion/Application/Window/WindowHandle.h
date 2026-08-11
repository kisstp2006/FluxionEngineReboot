#pragma once

#include <Fluxion/Foundation/Handle.h>

// Kept in its own tiny header so Events/Event.h can reference it without
// pulling in the rest of Window.h (which would create a circular include:
// Window.h needs event types for PollEvents, Event.h needs the window
// handle type).
FLUXION_DEFINE_HANDLE(FluxionWindowHandle);
