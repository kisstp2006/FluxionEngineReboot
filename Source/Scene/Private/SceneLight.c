// The three light components: registering them, and collecting them for
// a frame.
//
// Collecting is the interesting half. A renderer needs the lights as a
// flat list in world space, and the scene holds them as components on
// objects whose world matrices the transform update has just finished
// working out. So this reads both, and produces something RenderCore can
// take without RenderCore ever learning what a scene is.

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Light.h>

#include <math.h>
#include <string.h>

// --- Registering the three types -----------------------------------------

#define FLUXION_SCENE_DIRECTIONAL_PROPERTY_COUNT 1
#define FLUXION_SCENE_POINT_PROPERTY_COUNT       2
#define FLUXION_SCENE_SPOT_PROPERTY_COUNT        4
#define FLUXION_SCENE_ENVIRONMENT_PROPERTY_COUNT 2

static FluxionPropertyInfo s_directionalProperties[FLUXION_SCENE_DIRECTIONAL_PROPERTY_COUNT];
static FluxionPropertyInfo s_pointProperties[FLUXION_SCENE_POINT_PROPERTY_COUNT];
static FluxionPropertyInfo s_spotProperties[FLUXION_SCENE_SPOT_PROPERTY_COUNT];
static FluxionPropertyInfo s_environmentProperties[FLUXION_SCENE_ENVIRONMENT_PROPERTY_COUNT];

static FluxionTypeInfo s_directionalType;
static FluxionTypeInfo s_pointType;
static FluxionTypeInfo s_spotType;
static FluxionTypeInfo s_environmentType;

FluxionTypeId Fluxion_DirectionalLight_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionDirectionalLight); }
FluxionTypeId Fluxion_PointLight_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionPointLight); }
FluxionTypeId Fluxion_SpotLight_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionSpotLight); }
FluxionTypeId Fluxion_EnvironmentLight_TypeId(void) { return FLUXION_TYPE_ID_OF(FluxionEnvironmentLight); }

// Every field is written out, unlike the transform -- there is nothing
// here that is worked out from something else, so there is nothing to
// leave behind.
static void Fluxion_SceneLight_DescribeProperties(void)
{
    const FluxionPropertyInfo directional[FLUXION_SCENE_DIRECTIONAL_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionDirectionalLight, color, FLUXION_TYPE_ID_OF(FluxionVec3), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_directionalProperties, directional, sizeof(directional));

    const FluxionPropertyInfo point[FLUXION_SCENE_POINT_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionPointLight, color, FLUXION_TYPE_ID_OF(FluxionVec3), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionPointLight, range, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_pointProperties, point, sizeof(point));

    const FluxionPropertyInfo spot[FLUXION_SCENE_SPOT_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionSpotLight, color, FLUXION_TYPE_ID_OF(FluxionVec3), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionSpotLight, range, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionSpotLight, innerConeAngle, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionSpotLight, outerConeAngle, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_spotProperties, spot, sizeof(spot));

    // The asset reference is reflected like any other field, which is
    // what lets Fluxion_Scene_GatherAssetReferences find it: a build that
    // has to know which assets a scene reaches for cannot ask the scene,
    // it has to look at the declared type of every field.
    const FluxionPropertyInfo environment[FLUXION_SCENE_ENVIRONMENT_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionEnvironmentLight, environment, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionEnvironmentLight, intensity, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_environmentProperties, environment, sizeof(environment));
}

static bool Fluxion_SceneLight_RegisterOne(FluxionTypeInfo* type, const char* name, FluxionTypeId id, u32 size,
                                           FluxionPropertyInfo* properties, u32 propertyCount)
{
    // Asked of the registry rather than remembered in a flag: the
    // registry can be taken down and brought up again, and a flag would
    // then claim a type is registered when it no longer is.
    if (Fluxion_Reflection_FindTypeById(id) != NULL) return true;

    type->name = Fluxion_StringView_FromCStr(name);
    type->id = id;
    type->kind = FLUXION_TYPE_KIND_STRUCT;
    type->size = size;
    type->version = 1;
    type->members = Fluxion_Span_Make(properties, propertyCount, sizeof(FluxionPropertyInfo));
    type->methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

    return Fluxion_Reflection_RegisterType(type);
}

bool Fluxion_SceneLight_EnsureRegistered(void)
{
    if (!Fluxion_Reflection_IsInitialized()) return false;

    Fluxion_SceneLight_DescribeProperties();

    if (!Fluxion_SceneLight_RegisterOne(&s_directionalType, "FluxionDirectionalLight", Fluxion_DirectionalLight_TypeId(),
                                        (u32)sizeof(FluxionDirectionalLight), s_directionalProperties,
                                        FLUXION_SCENE_DIRECTIONAL_PROPERTY_COUNT))
        return false;

    if (!Fluxion_SceneLight_RegisterOne(&s_pointType, "FluxionPointLight", Fluxion_PointLight_TypeId(),
                                        (u32)sizeof(FluxionPointLight), s_pointProperties,
                                        FLUXION_SCENE_POINT_PROPERTY_COUNT))
        return false;

    if (!Fluxion_SceneLight_RegisterOne(&s_spotType, "FluxionSpotLight", Fluxion_SpotLight_TypeId(),
                                        (u32)sizeof(FluxionSpotLight), s_spotProperties,
                                        FLUXION_SCENE_SPOT_PROPERTY_COUNT))
        return false;

    return Fluxion_SceneLight_RegisterOne(&s_environmentType, "FluxionEnvironmentLight", Fluxion_EnvironmentLight_TypeId(),
                                          (u32)sizeof(FluxionEnvironmentLight), s_environmentProperties,
                                          FLUXION_SCENE_ENVIRONMENT_PROPERTY_COUNT);
}
