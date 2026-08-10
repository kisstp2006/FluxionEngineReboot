#include <Fluxion/Platform/DynamicLibrary.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

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

void* Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName)
{
    return (void*)GetProcAddress((HMODULE)library->handle, symbolName);
}
