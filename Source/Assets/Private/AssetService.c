#include <Fluxion/Assets/AssetService.h>

#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>

static FluxionAssetService s_service;
static bool s_registered = false;

FluxionServiceId Fluxion_AssetService_Id(void)
{
    return FLUXION_SERVICE_ID_OF(Assets);
}

bool Fluxion_AssetService_Register(void)
{
    if (s_registered) return true;

    s_service.header.serviceId = Fluxion_AssetService_Id();
    s_service.header.version = FLUXION_ASSET_SERVICE_VERSION;
    s_service.header.structSize = (u32)sizeof(FluxionAssetService);

    s_service.registerType = Fluxion_AssetTypes_Register;
    s_service.unregisterType = Fluxion_AssetTypes_Unregister;
    s_service.addAsset = Fluxion_AssetDatabase_Add;
    s_service.acquire = Fluxion_Assets_Acquire;
    s_service.release = Fluxion_Assets_Release;
    s_service.getState = Fluxion_Assets_GetState;
    s_service.getObject = Fluxion_Assets_GetObject;
    s_service.readFile = Fluxion_Vfs_ReadAll;
    s_service.freeBuffer = Fluxion_Vfs_FreeBuffer;

    s_registered = Fluxion_ServiceRegistry_Register(&s_service);
    return s_registered;
}

void Fluxion_AssetService_Unregister(void)
{
    if (!s_registered) return;

    Fluxion_ServiceRegistry_Unregister(Fluxion_AssetService_Id());
    s_registered = false;
}
