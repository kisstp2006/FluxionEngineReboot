// The transform component: registering it, and working out world
// matrices for a whole scene at once.
//
// Two things happen here that the on-demand path in Scene.c cannot do.
//
// The first is the previous-world copy. A renderer that compares one
// frame against the last needs where a thing was, and the only copy of
// that is the world matrix about to be overwritten. So every object's
// world is copied into its previous before anything is recomputed --
// every object, not only the ones that moved, because an object that
// stopped moving must report no motion rather than the motion it had two
// frames ago. As a column that is one memcpy per block.
//
// The second is doing the recomputation in parallel. A child cannot be
// worked out before its parent, which reads like a sequential problem
// and is not: objects at the same depth cannot be each other's parents,
// so a whole depth can go at once, and by the time a depth is reached
// every parent it needs is done. Depth by depth, each depth in parallel.

#include "SceneInternal.h"

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Scene/EntityQuery.h>

#include <string.h>

// How many objects one worker takes at a time. Small enough that a scene
// with a handful of objects at a depth still spreads over more than one
// worker, large enough that handing the work out does not cost more than
// the work.
#define FLUXION_SCENE_TRANSFORM_BATCH 64

// --- Registering the type ------------------------------------------------

// Only the local three are reflected. The world and previous-world
// matrices are worked out from those and from the hierarchy, so writing
// them out would be writing down an answer that has to be recomputed on
// the way back in anyway -- and one that would be wrong if the hierarchy
// were read back in a different order. The type's SIZE still covers the
// whole structure: one says how much room a component needs, the other
// says what is worth keeping.
// Filled in the first time the type is registered rather than written out
// as an initializer: both the name and the type id of a field are worked
// out by calling something, and C has no way to call anything while
// setting up a variable that outlives the call.
#define FLUXION_SCENE_TRANSFORM_PROPERTY_COUNT 3

static FluxionPropertyInfo s_transformProperties[FLUXION_SCENE_TRANSFORM_PROPERTY_COUNT];
static FluxionTypeInfo s_transformType;

static void Fluxion_SceneTransform_DescribeProperties(void)
{
    const FluxionPropertyInfo described[FLUXION_SCENE_TRANSFORM_PROPERTY_COUNT] =
    {
        FLUXION_REFLECT_PROPERTY(FluxionTransform, localPosition, FLUXION_TYPE_ID_OF(FluxionVec3), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionTransform, localRotation, FLUXION_TYPE_ID_OF(FluxionQuat), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(FluxionTransform, localScale, FLUXION_TYPE_ID_OF(FluxionVec3), FLUXION_PROPERTY_FLAG_NONE),
    };
    memcpy(s_transformProperties, described, sizeof(described));
}

FluxionTypeId Fluxion_Transform_TypeId(void)
{
    return FLUXION_TYPE_ID_OF(FluxionTransform);
}

bool Fluxion_SceneTransform_EnsureRegistered(void)
{
    const FluxionTypeId id = Fluxion_Transform_TypeId();

    // Asked rather than found out by tripping the assert inside: the
    // registry not being up is a startup mistake with a clear remedy, and
    // saying so here is the only place it can still be told apart from
    // "this type was never registered".
    FLUXION_ASSERT_MSG(Fluxion_Reflection_IsInitialized(),
        "Fluxion: a scene was created before the reflection registry was brought up -- "
        "every object carries a transform, and the storage takes its size from there");
    if (!Fluxion_Reflection_IsInitialized()) return false;

    // Asked of the registry rather than remembered in a flag: the registry
    // can be taken down and brought up again, and a flag would then say
    // the type is registered when it no longer is.
    if (Fluxion_Reflection_FindTypeById(id) != NULL) return true;

    Fluxion_SceneTransform_DescribeProperties();

    s_transformType.name = Fluxion_StringView_FromCStr("FluxionTransform");
    s_transformType.id = id;
    s_transformType.kind = FLUXION_TYPE_KIND_STRUCT;
    s_transformType.size = sizeof(FluxionTransform);
    s_transformType.version = 1;
    s_transformType.members = Fluxion_Span_Make(s_transformProperties,
        FLUXION_SCENE_TRANSFORM_PROPERTY_COUNT, sizeof(FluxionPropertyInfo));
    s_transformType.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

    return Fluxion_Reflection_RegisterType(&s_transformType);
}

// --- Reaching one object's transform -------------------------------------

FluxionTransform* Fluxion_SceneInternal_Transform(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return NULL;
    return (FluxionTransform*)Fluxion_SceneInternal_TransformOf(record, entry);
}

// --- The batched update --------------------------------------------------

// What one worker needs: which objects to do, and where to find them.
typedef struct FluxionSceneTransformBatch
{
    FluxionSceneRecord* record;
    const u32* objectIndices;
} FluxionSceneTransformBatch;

static void Fluxion_SceneTransform_UpdateOne(void* userData, u32 slot)
{
    FluxionSceneTransformBatch* batch = (FluxionSceneTransformBatch*)userData;
    FluxionSceneRecord* record = batch->record;
    FluxionSceneGameObjectRecord* entry = &record->objects[batch->objectIndices[slot]];
    FluxionTransform* transform = (FluxionTransform*)Fluxion_SceneInternal_TransformOf(record, entry);

    if (transform == NULL) return;
    if ((transform->dirtyFlags & FLUXION_TRANSFORM_DIRTY_WORLD) == 0) return;

    {
        const FluxionMat4 local = Fluxion_SceneInternal_LocalMatrixOf(transform);

        if (FLUXION_HANDLE_IS_VALID(entry->parent))
        {
            // The parent is at a lower depth and was therefore finished
            // before this depth began, so reading its world here needs no
            // guarding -- which is the whole reason the work is split by
            // depth rather than handed out object by object.
            const FluxionTransform* parent = Fluxion_SceneInternal_Transform(record, entry->parent);
            transform->worldMatrix = (parent != NULL)
                ? Fluxion_Mat4_Multiply(parent->worldMatrix, local)
                : local;
        }
        else
        {
            transform->worldMatrix = local;
        }

        transform->dirtyFlags = FLUXION_TRANSFORM_CLEAN;
    }
}

// Copies every object's world matrix into its previous. Done a column at
// a time, which is what this storage is for.
static void Fluxion_SceneTransform_CarryPrevious(FluxionSceneRecord* record)
{
    const FluxionTypeId required = Fluxion_Transform_TypeId();
    FluxionEntityQueryDesc desc;
    FluxionEntityQuery query;
    FluxionEntityChunkView chunk;

    desc.required = &required;
    desc.requiredCount = 1;
    desc.excluded = NULL;
    desc.excludedCount = 0;

    query = Fluxion_Scene_Query(record->self, &desc);
    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        FluxionTransform* transforms = (FluxionTransform*)Fluxion_EntityChunk_Column(&chunk, required);
        u32 row;
        if (transforms == NULL) continue;

        for (row = 0; row < chunk.count; ++row)
        {
            transforms[row].previousWorldMatrix = transforms[row].worldMatrix;
        }
    }
}

void Fluxion_SceneInternal_UpdateTransforms(FluxionSceneRecord* record)
{
    // Two scratch arrays over the object table: which objects need work,
    // grouped by depth. Both are bounded by the table, so they sit on the
    // stack rather than being allocated per step.
    static const u32 kMaxObjects = FLUXION_SCENE_MAX_GAME_OBJECTS;
    u32 pending[FLUXION_SCENE_MAX_GAME_OBJECTS];
    u32 pendingCount = 0;
    u32 maxDepth = 0;
    u32 i;

    if (record == NULL) return;

    // Nothing moved since the last update, and nothing moved during it
    // either -- so every previous already equals its world and every world
    // is already right. Skipping the copy matters: it is the one part that
    // would otherwise touch every object every step forever.
    if (!record->transformsDirty && !record->transformsChangedLastUpdate) return;

    Fluxion_SceneTransform_CarryPrevious(record);

    for (i = 0; i < kMaxObjects; ++i)
    {
        FluxionSceneGameObjectRecord* entry = &record->objects[i];
        const FluxionTransform* transform;

        if (!entry->alive) continue;

        transform = (const FluxionTransform*)Fluxion_SceneInternal_TransformOf(record, entry);
        if (transform == NULL) continue;
        if ((transform->dirtyFlags & FLUXION_TRANSFORM_DIRTY_WORLD) == 0) continue;

        pending[pendingCount++] = i;
        if (entry->depth > maxDepth) maxDepth = entry->depth;
    }

    record->transformsChangedLastUpdate = (pendingCount != 0);
    record->transformsDirty = false;

    if (pendingCount == 0) return;

    // Depth by depth. Within a depth no object is another's parent, so the
    // order inside it does not matter and the whole depth can go at once.
    {
        u32 depth;
        u32 atDepth[FLUXION_SCENE_MAX_GAME_OBJECTS];

        for (depth = 0; depth <= maxDepth; ++depth)
        {
            u32 atDepthCount = 0;
            FluxionSceneTransformBatch batch;

            for (i = 0; i < pendingCount; ++i)
            {
                if (record->objects[pending[i]].depth != depth) continue;
                atDepth[atDepthCount++] = pending[i];
            }
            if (atDepthCount == 0) continue;

            batch.record = record;
            batch.objectIndices = atDepth;

            // Handed to workers only when there are workers and enough to
            // be worth splitting. A caller with no job system running --
            // a test, a tool -- gets the same answers from the same code,
            // just on one thread.
            if (Fluxion_JobSystem_IsInitialized() && atDepthCount > FLUXION_SCENE_TRANSFORM_BATCH)
            {
                const FluxionJobHandle handle = Fluxion_JobSystem_ParallelFor(
                    atDepthCount, FLUXION_SCENE_TRANSFORM_BATCH,
                    Fluxion_SceneTransform_UpdateOne, &batch, NULL, 0);
                Fluxion_JobSystem_Wait(handle);
            }
            else
            {
                u32 slot;
                for (slot = 0; slot < atDepthCount; ++slot) Fluxion_SceneTransform_UpdateOne(&batch, slot);
            }
        }
    }
}
