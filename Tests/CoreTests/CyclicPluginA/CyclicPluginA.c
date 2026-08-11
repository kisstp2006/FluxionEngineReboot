#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Foundation/Defines.h>

// Never actually loaded by the cycle-detection test — CyclicPluginA and
// CyclicPluginB depend on each other, so LoadAll must fail during the
// topological sort, before either library is opened.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    FLUXION_UNUSED(host);
    outApi->userData = (void*)3;
    return true;
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);
}
