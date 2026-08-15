#include <Fluxion/Assets/AssetRef.h>

FluxionTypeId Fluxion_AssetRef_TypeId(void)
{
    // Behind a function so that one concept has one id. Hashing the name
    // at each call site is how two spellings of the same thing end up
    // being two different things that nothing notices are the same.
    return FLUXION_TYPE_ID_OF(AssetRef);
}
