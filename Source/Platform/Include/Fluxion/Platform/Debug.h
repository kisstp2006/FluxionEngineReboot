#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Fluxion_Platform_IsDebuggerAttached(void);

// Writes `message` to whatever debug output channel the platform offers
// (e.g. OutputDebugString on Windows), in addition to — not instead of —
// Foundation's Log. Intended for messages that should show up in an
// attached IDE debugger's output pane even when stdout/stderr aren't
// visible.
void Fluxion_Platform_DebugPrint(const char* message);

#ifdef __cplusplus
}
#endif
