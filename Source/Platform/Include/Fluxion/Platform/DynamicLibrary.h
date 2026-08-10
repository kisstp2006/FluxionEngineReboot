#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionDynamicLibrary
{
    void* handle;
} FluxionDynamicLibrary;

bool  Fluxion_Platform_LoadDynamicLibrary(FluxionDynamicLibrary* library, const char* path);
void  Fluxion_Platform_UnloadDynamicLibrary(FluxionDynamicLibrary* library);
void* Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName);

#ifdef __cplusplus
}
#endif
