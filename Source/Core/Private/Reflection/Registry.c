#include <Fluxion/Core/Reflection/Registry.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Hashing.h>

// FluxionTypeId -> const FluxionTypeInfo*. Fluxion_HashBytes64/BytesEqual
// work directly on the fixed-size FluxionTypeId (u64) key, same pattern as
// the Foundation HashMap tests.
static FluxionHashMap s_typeRegistry;
static bool s_reflectionInitialized = false;

void Fluxion_Reflection_Init(void)
{
    FLUXION_ASSERT_MSG(!s_reflectionInitialized, "Fluxion_Reflection_Init called twice without a Shutdown in between");
    Fluxion_HashMap_Init(&s_typeRegistry, NULL, sizeof(FluxionTypeId), sizeof(const FluxionTypeInfo*), Fluxion_HashBytes64, Fluxion_BytesEqual);
    s_reflectionInitialized = true;
}

void Fluxion_Reflection_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_reflectionInitialized, "Fluxion_Reflection_Shutdown called before Init");
    Fluxion_HashMap_Destroy(&s_typeRegistry);
    s_reflectionInitialized = false;
}

bool Fluxion_Reflection_IsInitialized(void)
{
    return s_reflectionInitialized;
}

bool Fluxion_Reflection_RegisterType(const FluxionTypeInfo* typeInfo)
{
    FLUXION_ASSERT(s_reflectionInitialized);
    FLUXION_ASSERT(typeInfo != NULL);

    return Fluxion_HashMap_Set(&s_typeRegistry, &typeInfo->id, &typeInfo);
}

bool Fluxion_Reflection_UnregisterType(FluxionTypeId id)
{
    FLUXION_ASSERT(s_reflectionInitialized);
    return Fluxion_HashMap_Remove(&s_typeRegistry, &id);
}

const FluxionTypeInfo* Fluxion_Reflection_FindTypeById(FluxionTypeId id)
{
    FLUXION_ASSERT(s_reflectionInitialized);

    const FluxionTypeInfo** found = (const FluxionTypeInfo**)Fluxion_HashMap_Find(&s_typeRegistry, &id);
    return found ? *found : NULL;
}

const FluxionTypeInfo* Fluxion_Reflection_FindTypeByName(FluxionStringView name)
{
    // TypeIds are derived from the name hash (see FLUXION_TYPE_ID_OF), so a
    // name lookup is just a ById lookup keyed by that same hash.
    return Fluxion_Reflection_FindTypeById(Fluxion_TypeId_FromName(name));
}
