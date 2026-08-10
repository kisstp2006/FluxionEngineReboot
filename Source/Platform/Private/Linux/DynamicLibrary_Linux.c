#include <Fluxion/Platform/DynamicLibrary.h>

#include <dlfcn.h>

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

void* Fluxion_Platform_GetSymbol(FluxionDynamicLibrary* library, const char* symbolName)
{
    return dlsym(library->handle, symbolName);
}
