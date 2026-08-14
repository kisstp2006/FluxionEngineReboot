#include <Fluxion/Platform/DynamicLibrary.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdio.h>

bool Fluxion_Platform_LoadDynamicLibrary(FluxionDynamicLibrary* library, const char* path)
{
    HMODULE module = LoadLibraryA(path);
    library->handle = (void*)module;
    return module != NULL;
}

void Fluxion_Platform_UnloadDynamicLibrary(FluxionDynamicLibrary* library)
{
    if (library->handle)
    {
        FreeLibrary((HMODULE)library->handle);
        library->handle = NULL;
    }
}

FluxionSymbolAddress Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName)
{
    // GetProcAddress already returns a function pointer, so this stays a
    // conversion between two function pointer types -- the kind the
    // language does define.
    return (FluxionSymbolAddress)GetProcAddress((HMODULE)library->handle, symbolName);
}

bool Fluxion_Platform_GetDynamicLibraryFileName(const char* baseName, char* outBuffer, usize bufferSize)
{
    int written = snprintf(outBuffer, bufferSize, "%s.dll", baseName);
    return written > 0 && (usize)written < bufferSize;
}
