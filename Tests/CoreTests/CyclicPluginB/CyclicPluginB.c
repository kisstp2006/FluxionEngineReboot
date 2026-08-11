#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Foundation/Defines.h>

// See CyclicPluginA.c — never actually loaded, this pair exists to prove
// dependency-cycle detection.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    FLUXION_UNUSED(host);
    outApi->userData = (void*)4;
    return true;
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);
}
