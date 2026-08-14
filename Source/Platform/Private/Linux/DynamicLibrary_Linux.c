#include <Fluxion/Platform/DynamicLibrary.h>

#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

bool Fluxion_Platform_LoadDynamicLibrary(FluxionDynamicLibrary* library, const char* path)
{
    library->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    return library->handle != NULL;
}

void Fluxion_Platform_UnloadDynamicLibrary(FluxionDynamicLibrary* library)
{
    if (library->handle)
    {
        dlclose(library->handle);
        library->handle = NULL;
    }
}

FluxionSymbolAddress Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName)
{
    // dlsym hands back void*, which C does not allow converting to a
    // function pointer -- POSIX requires the two to be interchangeable
    // anyway, and copying the representation says exactly that without
    // asking the language for a conversion it does not define.
    void* raw = dlsym(library->handle, symbolName);
    FluxionSymbolAddress symbol = NULL;
    memcpy(&symbol, &raw, sizeof(symbol));
    return symbol;
}

bool Fluxion_Platform_GetDynamicLibraryFileName(const char* baseName, char* outBuffer, usize bufferSize)
{
    int written = snprintf(outBuffer, bufferSize, "lib%s.so", baseName);
    return written > 0 && (usize)written < bufferSize;
}
