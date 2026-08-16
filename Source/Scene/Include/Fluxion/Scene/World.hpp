// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#pragma once

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SystemScheduler.h>

#include <span>
#include <type_traits>

// A C++ way of saying the same things Scene.h says.
//
// Everything here forwards to the interface in Scene.h and holds nothing
// of its own beyond the two handles it was built from: a World is a scene
// handle, an Entity is a scene handle and an object handle. Both are
// values, both are cheap to copy, and neither keeps anything alive.
//
// What this adds over calling the C interface directly is that a component
// is named by its type rather than by an id worked out at the call site,
// and that the storage handed back is typed. Mixing the two is fine --
// they are the same objects and the same components, reached two ways.

namespace Fluxion::Scene
{

// A component type: a plain struct that declares the name it is reflected
// under, the same way every other reflectable type in the engine does.
//
// Trivially copyable because the storage moves components with a plain
// byte copy when an entity changes which components it carries -- a type
// with a copy constructor to run would be moved without running it.
//
// The alignment limit is what the storage aligns its columns to. A type
// wanting more would be handed a pointer that does not meet its own
// requirement, which on some architectures is a fault and on others is a
// silent slowdown; either way it is caught here, when the type is named,
// rather than at the first read.
template<typename T>
concept ComponentType = Core::ReflectableType<T>
    && std::is_trivially_copyable_v<T>
    && (alignof(T) <= FLUXION_DEFAULT_ALIGNMENT);

class Entity
{
public:
    Entity() : m_scene(Fluxion_Scene_InvalidHandle()), m_handle(Fluxion_GameObject_InvalidHandle()) {}
    Entity(FluxionSceneHandle scene, FluxionGameObjectHandle handle) : m_scene(scene), m_handle(handle) {}

    FluxionSceneHandle SceneHandle() const { return m_scene; }
    FluxionGameObjectHandle Handle() const { return m_handle; }

    bool IsValid() const { return Fluxion_GameObject_IsValid(m_scene, m_handle); }
    explicit operator bool() const { return IsValid(); }

    // Two entities are the same when they are the same object of the same
    // scene. An object destroyed and its slot handed out again gives an
    // entity that is not equal to the old one, because the generation
    // moved on.
    bool operator==(const Entity& other) const
    {
        return m_scene.index == other.m_scene.index
            && m_scene.generation == other.m_scene.generation
            && m_handle.index == other.m_handle.index
            && m_handle.generation == other.m_handle.generation;
    }

    void Destroy() { Fluxion_GameObject_Destroy(m_scene, m_handle); }

    const char* Name() const { return Fluxion_GameObject_GetName(m_scene, m_handle); }
    void SetName(const char* name) { Fluxion_GameObject_SetName(m_scene, m_handle, name); }

    FluxionUUID UUID() const { return Fluxion_GameObject_GetUUID(m_scene, m_handle); }

    // --- Hierarchy ------------------------------------------------------

    Entity Parent() const { return Entity(m_scene, Fluxion_GameObject_GetParent(m_scene, m_handle)); }
    Entity FirstChild() const { return Entity(m_scene, Fluxion_GameObject_GetFirstChild(m_scene, m_handle)); }
    Entity NextSibling() const { return Entity(m_scene, Fluxion_GameObject_GetNextSibling(m_scene, m_handle)); }
    u32 ChildCount() const { return Fluxion_GameObject_GetChildCount(m_scene, m_handle); }

    // A default-built Entity puts this one back up among the scene's
    // roots, which is what an invalid parent handle means to the interface
    // underneath.
    void SetParent(Entity parent) { Fluxion_GameObject_SetParent(m_scene, m_handle, parent.m_handle); }

    Entity FindChild(const char* name) const { return Entity(m_scene, Fluxion_GameObject_FindChild(m_scene, m_handle, name)); }
    Entity FindChildRecursive(const char* name) const { return Entity(m_scene, Fluxion_GameObject_FindChildRecursive(m_scene, m_handle, name)); }

    // --- Transform ------------------------------------------------------

    FluxionVec3 LocalPosition() const { return Fluxion_GameObject_GetLocalPosition(m_scene, m_handle); }
    void SetLocalPosition(FluxionVec3 position) { Fluxion_GameObject_SetLocalPosition(m_scene, m_handle, position); }

    FluxionQuat LocalRotation() const { return Fluxion_GameObject_GetLocalRotation(m_scene, m_handle); }
    void SetLocalRotation(FluxionQuat rotation) { Fluxion_GameObject_SetLocalRotation(m_scene, m_handle, rotation); }

    FluxionVec3 LocalScale() const { return Fluxion_GameObject_GetLocalScale(m_scene, m_handle); }
    void SetLocalScale(FluxionVec3 scale) { Fluxion_GameObject_SetLocalScale(m_scene, m_handle, scale); }

    void Rotate(FluxionVec3 eulerRadians) { Fluxion_GameObject_Rotate(m_scene, m_handle, eulerRadians); }

    FluxionMat4 WorldMatrix() const { return Fluxion_GameObject_GetWorldMatrix(m_scene, m_handle); }
    FluxionMat4 LocalMatrix() const { return Fluxion_GameObject_GetLocalMatrix(m_scene, m_handle); }

    // --- Components -----------------------------------------------------

    // The reference is good until the next call that adds or removes a
    // component ANYWHERE in this scene, of any type -- the same rule the
    // interface underneath states for its pointer, and for the same
    // reason: an entity's components are stored with those of every other
    // entity carrying the same set, so changing that set moves all of
    // them. Take a copy, or ask again.
    template<ComponentType T>
    T* Add(const T& value)
    {
        return static_cast<T*>(Fluxion_GameObject_AddComponent(m_scene, m_handle, Core::TypeIdOf<T>(), &value));
    }

    // Starts as all zero bytes.
    template<ComponentType T>
    T* Add()
    {
        return static_cast<T*>(Fluxion_GameObject_AddComponent(m_scene, m_handle, Core::TypeIdOf<T>(), nullptr));
    }

    // Null when this entity carries none of this type.
    template<ComponentType T>
    T* Get() const
    {
        return static_cast<T*>(Fluxion_GameObject_GetComponent(m_scene, m_handle, Core::TypeIdOf<T>()));
    }

    template<ComponentType T>
    bool Has() const
    {
        return Fluxion_GameObject_HasComponent(m_scene, m_handle, Core::TypeIdOf<T>());
    }

    template<ComponentType T>
    bool Remove()
    {
        return Fluxion_GameObject_RemoveComponent(m_scene, m_handle, Core::TypeIdOf<T>());
    }

    // Which component types this entity carries, written into `outTypes`,
    // and how many there are. What writing an entity out needs, and the
    // only way to ask: every other question here needs the type named in
    // advance, which is the thing being asked for.
    u32 ComponentTypes(FluxionTypeId* outTypes, u32 maxTypes) const
    {
        return Fluxion_GameObject_GetComponentTypes(m_scene, m_handle, outTypes, maxTypes);
    }

    u32 ComponentCount() const { return Fluxion_GameObject_GetComponentTypes(m_scene, m_handle, nullptr, 0); }

private:
    FluxionSceneHandle m_scene;
    FluxionGameObjectHandle m_handle;
};

class World
{
public:
    // Makes a scene and takes charge of it.
    World() : m_handle(Fluxion_Scene_Create()), m_owned(true) {}

    // Stands in for a scene somebody else made and keeps in charge of. The
    // World going away leaves the scene where it was.
    static World Borrow(FluxionSceneHandle scene) { return World(scene, false); }

    ~World() { Release(); }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept : m_handle(other.m_handle), m_owned(other.m_owned)
    {
        other.m_handle = Fluxion_Scene_InvalidHandle();
        other.m_owned = false;
    }

    World& operator=(World&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_handle = other.m_handle;
            m_owned = other.m_owned;
            other.m_handle = Fluxion_Scene_InvalidHandle();
            other.m_owned = false;
        }
        return *this;
    }

    FluxionSceneHandle Handle() const { return m_handle; }
    bool IsValid() const { return Fluxion_Scene_IsValid(m_handle); }
    explicit operator bool() const { return IsValid(); }

    Entity Create(const char* name = nullptr) { return Entity(m_handle, Fluxion_Scene_CreateGameObject(m_handle, name)); }
    Entity CreateWithUUID(const char* name, FluxionUUID uuid) { return Entity(m_handle, Fluxion_Scene_CreateGameObjectWithUUID(m_handle, name, uuid)); }

    Entity Find(const char* name) const { return Entity(m_handle, Fluxion_Scene_Find(m_handle, name)); }
    Entity FindByUUID(FluxionUUID uuid) const { return Entity(m_handle, Fluxion_Scene_FindByUUID(m_handle, uuid)); }

    Entity FirstRoot() const { return Entity(m_handle, Fluxion_Scene_GetFirstRoot(m_handle)); }
    u32 EntityCount() const { return Fluxion_Scene_GameObjectCount(m_handle); }

    void Tick(f32 deltaTime) { Fluxion_Scene_Tick(m_handle, deltaTime); }

    const char* LastError() const { return Fluxion_Scene_GetLastError(m_handle); }

    // The scene's own, played back at the end of each Tick. Null when this
    // World stands for no live scene.
    FluxionEntityCommandBuffer* Commands() { return Fluxion_Scene_GetCommandBuffer(m_handle); }

    // --- Reading many entities at once ----------------------------------

    // Every entity carrying all of these component types, a block at a
    // time. The callable is handed the entities of one block and, for each
    // named type, that block's values for exactly those entities -- laid
    // out one after another with nothing in between:
    //
    //   world.EachChunk<Position, Velocity>(
    //       [](std::span<const FluxionEntityHandle> entities,
    //          std::span<Position> positions,
    //          std::span<Velocity> velocities) { ... });
    //
    // All the spans of one call have the same length, and entry i of each
    // belongs to entry i of the entities. An entity carrying more than the
    // named types still matches: a query says what it needs, not what an
    // entity is allowed to be.
    //
    // This is the shape worth reaching for when the work per entity is
    // small, because it is where the storage's whole purpose shows up --
    // the values are already next to each other and the loop is a straight
    // run over them.
    //
    // NOTHING STRUCTURAL MAY HAPPEN INSIDE THE CALLABLE. Adding a
    // component, removing one, or destroying an entity moves the very
    // values being read. Record such changes into Commands() and let them
    // land after the walk.
    template<ComponentType... Ts, typename Fn>
    void EachChunk(Fn&& fn) const
    {
        const FluxionTypeId required[] = { Core::TypeIdOf<Ts>()... };

        FluxionEntityQueryDesc desc{};
        desc.required = required;
        desc.requiredCount = (u32)sizeof...(Ts);

        FluxionEntityQuery query = Fluxion_Scene_Query(m_handle, &desc);
        FluxionEntityChunkView chunk;
        while (Fluxion_EntityQuery_Next(&query, &chunk))
        {
            fn(std::span<const FluxionEntityHandle>(chunk.entities, chunk.count),
               std::span<Ts>(static_cast<Ts*>(Fluxion_EntityChunk_Column(&chunk, Core::TypeIdOf<Ts>())), chunk.count)...);
        }
    }

    // The same set of entities, one at a time:
    //
    //   world.Each<Position, Velocity>(
    //       [](Entity e, Position& p, Velocity& v) { ... });
    //
    // Easier to write and easier to get right; the block form above is
    // what to reach for when the per-entity work is small enough that the
    // call around it would dominate.
    //
    // The same rule holds: nothing structural inside the callable.
    template<ComponentType... Ts, typename Fn>
    void Each(Fn&& fn) const
    {
        const FluxionSceneHandle scene = m_handle;
        EachChunk<Ts...>([&fn, scene](std::span<const FluxionEntityHandle> entities, std::span<Ts>... columns)
        {
            for (usize i = 0; i < entities.size(); ++i)
            {
                fn(Entity(scene, entities[i]), columns[i]...);
            }
        });
    }

    // How many entities carry all of these types, without reading any of
    // their values.
    template<ComponentType... Ts>
    u32 CountWith() const
    {
        const FluxionTypeId required[] = { Core::TypeIdOf<Ts>()... };

        FluxionEntityQueryDesc desc{};
        desc.required = required;
        desc.requiredCount = (u32)sizeof...(Ts);
        return Fluxion_Scene_CountMatching(m_handle, &desc);
    }

    template<ComponentType T>
    u32 ComponentCount() const
    {
        return Fluxion_Scene_ComponentCount(m_handle, Core::TypeIdOf<T>());
    }

    // --- Systems ---------------------------------------------------------

    // What a system reads and what it changes, written as types rather
    // than as ids worked out at the call site:
    //
    //   world.AddSystem<Reads<Velocity>, Writes<Position>>(
    //       "Movement", FLUXION_SYSTEM_PHASE_SIMULATION, &Move);
    //
    // Both lists may be empty. A system that names nothing conflicts with
    // nothing and may run beside anything -- which is only true if it
    // really touches nothing, and is checked.
    template<ComponentType... Ts> struct Reads {};
    template<ComponentType... Ts> struct Writes {};

    template<typename ReadSet = Reads<>, typename WriteSet = Writes<>>
    FluxionSystemHandle AddSystem(const char* name, FluxionSystemPhase phase, FluxionSystemFn run, void* userData = nullptr)
    {
        FluxionSystemDesc desc{};
        desc.name = name;
        desc.phase = phase;
        desc.run = run;
        desc.userData = userData;
        return AddSystemWithSets(desc, ReadSet{}, WriteSet{});
    }

    // The same, for a system that has to be told which others it runs
    // before or after. The arrays are only read while this call runs.
    template<typename ReadSet = Reads<>, typename WriteSet = Writes<>>
    FluxionSystemHandle AddOrderedSystem(const char* name, FluxionSystemPhase phase, FluxionSystemFn run,
                                         std::span<const char* const> after,
                                         std::span<const char* const> before = {},
                                         void* userData = nullptr)
    {
        FluxionSystemDesc desc{};
        desc.name = name;
        desc.phase = phase;
        desc.run = run;
        desc.userData = userData;
        desc.executeAfter = after.data();
        desc.executeAfterCount = (u32)after.size();
        desc.executeBefore = before.data();
        desc.executeBeforeCount = (u32)before.size();
        return AddSystemWithSets(desc, ReadSet{}, WriteSet{});
    }

    bool RemoveSystem(FluxionSystemHandle system) { return Fluxion_Scene_RemoveSystem(m_handle, system); }
    u32 SystemCount() const { return Fluxion_Scene_SystemCount(m_handle); }

private:
    template<ComponentType... Rs, ComponentType... Ws>
    FluxionSystemHandle AddSystemWithSets(FluxionSystemDesc& desc, Reads<Rs...>, Writes<Ws...>)
    {
        const FluxionTypeId reads[] = { Core::TypeIdOf<Rs>()..., FLUXION_TYPE_ID_INVALID };
        const FluxionTypeId writes[] = { Core::TypeIdOf<Ws>()..., FLUXION_TYPE_ID_INVALID };

        // The trailing invalid entry is there so the arrays are never zero
        // length, which C++ does not allow; the counts are what is read.
        desc.reads = reads;
        desc.readCount = (u32)sizeof...(Rs);
        desc.writes = writes;
        desc.writeCount = (u32)sizeof...(Ws);
        return Fluxion_Scene_AddSystem(m_handle, &desc);
    }

    World(FluxionSceneHandle handle, bool owned) : m_handle(handle), m_owned(owned) {}

    void Release()
    {
        if (m_owned && Fluxion_Scene_IsValid(m_handle)) Fluxion_Scene_Destroy(m_handle);
        m_handle = Fluxion_Scene_InvalidHandle();
        m_owned = false;
    }

    FluxionSceneHandle m_handle;
    bool m_owned;
};

} // namespace Fluxion::Scene
