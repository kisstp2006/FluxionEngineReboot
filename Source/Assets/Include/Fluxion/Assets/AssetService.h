#pragma once

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// How a plugin adds an asset type.
//
// THROUGH THE SERVICE REGISTRY, NOT THROUGH THE PLUGIN INTERFACE. A
// plugin already gets a way to ask the host for a service by name; using
// it here means that adding a new kind of asset does not raise the host
// interface's version number. Raising that number makes every plugin
// built against the old one out of date at once, and "someone wrote an
// importer" is not a reason for that to happen.
//
// A PLUGIN THAT REGISTERS A TYPE MUST UNREGISTER IT BEFORE IT UNLOADS.
// The descriptor is copied, so its bytes are safe; the functions in it
// are not. They live in the plugin's own library, and unloading that
// library leaves them pointing at nothing that will say so.

#define FLUXION_ASSET_SERVICE_VERSION 1

typedef struct FluxionAssetService
{
    FluxionServiceHeader header;

    bool (*registerType)(const FluxionAssetTypeDesc* desc);
    bool (*unregisterType)(FluxionAssetTypeId id);

    bool (*addAsset)(const FluxionAssetDesc* desc, FluxionUUID* outId);

    FluxionAssetHandle (*acquire)(FluxionUUID id);
    void (*release)(FluxionAssetHandle handle);
    FluxionAssetState (*getState)(FluxionAssetHandle handle);
    void* (*getObject)(FluxionAssetHandle handle);

    // An importer reads its source file through the mount layer like
    // everything else, rather than opening a path -- so an importer
    // written today keeps working where paths do not exist.
    u8* (*readFile)(const char* path, usize* outSize);
    void (*freeBuffer)(u8* buffer, usize size);
} FluxionAssetService;

FluxionServiceId Fluxion_AssetService_Id(void);

bool Fluxion_AssetService_Register(void);
void Fluxion_AssetService_Unregister(void);

#ifdef __cplusplus
}
#endif
