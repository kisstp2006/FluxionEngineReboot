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

// Turns a bare library name (e.g. "HelloPlugin") into the OS-specific file
// name callers actually need to load ("HelloPlugin.dll" on Windows,
// "libHelloPlugin.so" on Linux). Returns false if it doesn't fit in
// outBuffer.
bool Fluxion_Platform_GetDynamicLibraryFileName(const char* baseName, char* outBuffer, usize bufferSize);

#ifdef __cplusplus
}
#endif
