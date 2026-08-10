#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionThread
{
    void* handle;
} FluxionThread;

typedef void (*FluxionThreadFn)(void* userData);

// `name` may be NULL. On success the thread is running; call either
// ThreadJoin or ThreadDetach exactly once to release the handle.
bool Fluxion_Platform_ThreadCreate(FluxionThread* thread, FluxionThreadFn fn, void* userData, const char* name);
void Fluxion_Platform_ThreadJoin(FluxionThread* thread);
void Fluxion_Platform_ThreadDetach(FluxionThread* thread);

// Renames the CALLING thread (not `thread` above). Both backends could
// technically rename an arbitrary thread given its handle, but "rename
// yourself right after you start" (or rename main()) covers the real use
// cases without keeping thread handles around just for naming.
void Fluxion_Platform_SetCurrentThreadName(const char* name);

u64 Fluxion_Platform_GetCurrentThreadId(void);

#ifdef __cplusplus
}
#endif
