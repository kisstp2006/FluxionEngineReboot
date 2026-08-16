#pragma once

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Scene/Scene.h>

// Declared rather than included: this header describes what a light IS,
// and the shape a renderer wants one in belongs to the renderer.
struct FluxionRenderLight;

#ifdef __cplusplus
extern "C" {
#endif

// A light is something an object HAS, not something a view was told
// about.
//
// Before this, the one light in the engine was a field on the render
// view, filled in by whoever opened the frame. That was enough for a
// single light nobody moved. A light on an object can be moved, turned
// off, copied, saved with the scene and read back with it -- and none of
// those are things a field on a view can do.
//
// WHERE THE LIGHT IS DOES NOT LIVE HERE. The position and the direction
// come from the object's transform, and are not repeated in the
// component: two places saying where a light is can disagree, and the
// disagreement is light arriving from somewhere nothing points at, with
// nothing to report it. A directional light shines along the object's
// forward axis; a point light sits at the object's position; a spot does
// both.
//
// THE COLOUR IS THE INTENSITY. There is no separate brightness field
// here, exactly as there is none anywhere else in this engine: a colour
// twice as bright IS twice the light. That is what makes a real camera
// exposure meaningful -- see Fluxion_Exposure_FromCamera.

// Light that arrives from one direction, everywhere, with no falloff --
// a sun. Distance to it is not a thing that exists.
typedef struct FluxionDirectionalLight
{
    FluxionVec3 color;
} FluxionDirectionalLight;

// Light from a point in space, spreading in every direction.
typedef struct FluxionPointLight
{
    FluxionVec3 color;

    // Where this light's contribution reaches zero.
    //
    // Not a physical quantity -- real light never stops -- but a
    // necessary one: without a distance at which a light can be ignored,
    // every light has to be considered for every pixel, and that is what
    // makes many lights unaffordable. The falloff is shaped so that it
    // arrives at exactly zero here rather than merely small, because a
    // light dropped while it was still contributing leaves a visible edge
    // where it was dropped.
    f32 range;
} FluxionPointLight;

// A point light with a cone over it.
typedef struct FluxionSpotLight
{
    FluxionVec3 color;
    f32 range;

    // Radians from the axis. Full brightness within the inner angle,
    // nothing beyond the outer, and a smooth transition between them.
    //
    // Two angles rather than one and a softness, because that is how the
    // two are authored and how every exchange format stores them -- and a
    // softness would have to be turned back into these to be used.
    f32 innerConeAngle;
    f32 outerConeAngle;
} FluxionSpotLight;

// What the world looks like in every direction: a sky, and the light it
// casts on everything.
//
// It is a light and not a background. The sky is what fills in every
// direction nothing else covers, and once the irradiance from it is being
// worked out it is also the largest light source in most scenes -- the
// reason an object's shaded side is blue outdoors rather than black.
// Drawing it behind everything is a consequence of having it, not the
// purpose.
//
// THE ENVIRONMENT IS AN ASSET REFERENCE, not a texture handle. A scene
// saved with a handle in it would come back pointing at whatever occupied
// that slot next; an id is a thing that survives being written down --
// which is the same reason every other asset in a scene is one.
//
// Which SHAPE that asset has is not a separate kind of asset: a sky is a
// Texture whose dimension is a cube. Six images, one equirectangular
// file, or a cross layout are all ways of IMPORTING one, and the importer
// lives in a plugin -- so a built game carries no reader for any of them.
typedef struct FluxionEnvironmentLight
{
    FluxionAssetRef environment;

    // A multiplier over what the asset holds. The environment is stored
    // in real units like everything else, so this is not a brightness
    // slider: it is for a scene that wants the same sky dimmer than it
    // was captured, without a second copy of it.
    f32 intensity;
} FluxionEnvironmentLight;

// The ids these are registered under. A script naming the same type names
// the same id, so a script's light and a stored light are one thing.
FluxionTypeId Fluxion_DirectionalLight_TypeId(void);
FluxionTypeId Fluxion_PointLight_TypeId(void);
FluxionTypeId Fluxion_SpotLight_TypeId(void);
FluxionTypeId Fluxion_EnvironmentLight_TypeId(void);

// Every light in the scene, flattened into world space, in the shape a
// renderer takes.
//
// Returns HOW MANY THERE ARE, not how many fitted -- a caller sizes its
// next call from this, so answering with what it could hold would mean it
// never learned it needed more. Call with a null buffer to ask only the
// count.
//
// Reads the world matrices as they stand, so it belongs after the
// transform update and before the frame is drawn.
u32 Fluxion_Scene_GatherLights(FluxionSceneHandle scene, struct FluxionRenderLight* outLights, u32 capacity);

// The scene's sky, if it has one.
//
// One rather than a list: a scene is inside one world, and two skies is
// not a thing that has an answer. When more than one object carries the
// component the first found wins and the rest are reported -- silently
// picking one would make which sky you got depend on the order objects
// happened to be created in.
//
// False when nothing in the scene carries the component, which is not an
// error: a scene with no sky is lit by its lights alone.
bool Fluxion_Scene_GatherEnvironment(FluxionSceneHandle scene, FluxionEnvironmentLight* outEnvironment);

#ifdef __cplusplus
}
#endif
