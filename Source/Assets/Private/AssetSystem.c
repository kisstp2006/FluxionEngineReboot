#include <Fluxion/Assets/AssetSystem.h>

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetService.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Log.h>

#define FLUXION_ASSET_SYSTEM_LOG_CATEGORY "Assets"

static bool s_initialized = false;

bool Fluxion_AssetSystem_Init(FluxionAllocator* allocator)
{
    if (s_initialized) return true;

    Fluxion_Vfs_Init(allocator);
    Fluxion_AssetTypes_Init(allocator);
    Fluxion_AssetDatabase_Init(allocator);
    Fluxion_Assets_Init(allocator);

    if (!Fluxion_AssetService_Register())
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_SYSTEM_LOG_CATEGORY, "could not publish the asset service; no plugin would be able to add a type");

        Fluxion_Assets_Shutdown();
        Fluxion_AssetDatabase_Shutdown();
        Fluxion_AssetTypes_Shutdown();
        Fluxion_Vfs_Shutdown();
        return false;
    }

    s_initialized = true;
    return true;
}

void Fluxion_AssetSystem_Shutdown(void)
{
    if (!s_initialized) return;

    // The service goes first: it hands out function pointers into
    // everything below, and it must stop doing that before any of them
    // stops being there.
    Fluxion_AssetService_Unregister();

    Fluxion_Assets_Shutdown();
    Fluxion_AssetDatabase_Shutdown();
    Fluxion_AssetTypes_Shutdown();
    Fluxion_Vfs_Shutdown();

    s_initialized = false;
}

bool Fluxion_AssetSystem_IsInitialized(void)
{
    return s_initialized;
}
