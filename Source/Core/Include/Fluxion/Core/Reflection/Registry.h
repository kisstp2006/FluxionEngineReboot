#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Not thread-safe yet. Callers own the FluxionTypeInfo they register: the
// registry only stores the pointer, it does not copy or take ownership.
void Fluxion_Reflection_Init(void);
void Fluxion_Reflection_Shutdown(void);

bool Fluxion_Reflection_RegisterType(const FluxionTypeInfo* typeInfo);
const FluxionTypeInfo* Fluxion_Reflection_FindTypeById(FluxionTypeId id);
const FluxionTypeInfo* Fluxion_Reflection_FindTypeByName(FluxionStringView name);

#ifdef __cplusplus
}
#endif
