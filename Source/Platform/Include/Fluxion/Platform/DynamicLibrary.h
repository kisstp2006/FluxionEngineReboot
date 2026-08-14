#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionDynamicLibrary
{
    void* handle;
} FluxionDynamicLibrary;

// What a looked-up symbol is: an address to call, not an object to read.
// Callers cast it to the exact signature they expect -- converting one
// function pointer type to another and back is well defined, whereas
// going through void* is not, which is why this is not void*.
typedef void (*FluxionSymbolAddress)(void);

bool Fluxion_Platform_LoadDynamicLibrary(FluxionDynamicLibrary* library, const char* path);
void Fluxion_Platform_UnloadDynamicLibrary(FluxionDynamicLibrary* library);

// NULL if the library has no such symbol. Each platform's own loader
// already deals in one kind or the other -- Windows hands back a function
// pointer, Linux an object pointer -- so the one conversion that has to
// happen happens there, once, instead of at every call site.
FluxionSymbolAddress Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName);

// Turns a bare library name (e.g. "HelloPlugin") into the OS-specific file
// name callers actually need to load ("HelloPlugin.dll" on Windows,
// "libHelloPlugin.so" on Linux). Returns false if it doesn't fit in
// outBuffer.
bool Fluxion_Platform_GetDynamicLibraryFileName(const char* baseName, char* outBuffer, usize bufferSize);

#ifdef __cplusplus
}
#endif
