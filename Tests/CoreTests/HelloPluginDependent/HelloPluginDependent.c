#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Foundation/Defines.h>

FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    FLUXION_UNUSED(host);
    outApi->userData = (void*)2;
    return true;
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);
}
