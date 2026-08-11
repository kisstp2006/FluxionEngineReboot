#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionPluginType
{
    FLUXION_PLUGIN_TYPE_RUNTIME = 0,
    FLUXION_PLUGIN_TYPE_RENDERER,
    FLUXION_PLUGIN_TYPE_RHI,
    FLUXION_PLUGIN_TYPE_PHYSICS,
    FLUXION_PLUGIN_TYPE_AUDIO,
    FLUXION_PLUGIN_TYPE_IMPORTER,
    FLUXION_PLUGIN_TYPE_EDITOR,
    FLUXION_PLUGIN_TYPE_DEVELOPER,
    FLUXION_PLUGIN_TYPE_GAME,
} FluxionPluginType;

#define FLUXION_PLUGIN_MAX_NAME_LENGTH  63
#define FLUXION_PLUGIN_MAX_DEPENDENCIES 8

// Fully POD, fixed-size, no owned heap memory — a small, generous cap on
// names/dependencies is simpler and cheaper than managing the lifetime of
// a dynamic string array for something this small.
typedef struct FluxionPluginDescriptor
{
    char name[FLUXION_PLUGIN_MAX_NAME_LENGTH + 1];
    char library[FLUXION_PLUGIN_MAX_NAME_LENGTH + 1]; // base name, no prefix/extension
    u32 version;
    FluxionPluginType type;
    char dependencies[FLUXION_PLUGIN_MAX_DEPENDENCIES][FLUXION_PLUGIN_MAX_NAME_LENGTH + 1];
    u32 dependencyCount;
} FluxionPluginDescriptor;

#ifdef __cplusplus
}
#endif
