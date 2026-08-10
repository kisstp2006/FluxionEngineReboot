#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 Fluxion_Platform_GetCurrentProcessId(void);

// Terminates the current process immediately (no atexit/destructor
// unwinding). Not exercised by the automated tests — calling it would
// kill the test runner.
void Fluxion_Platform_ExitProcess(i32 exitCode);

#ifdef __cplusplus
}
#endif
