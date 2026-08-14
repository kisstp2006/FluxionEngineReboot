#pragma once

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/Scene.h>

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
template<typename T>
concept ComponentType = Core::ReflectableType<T> && std::is_trivially_copyable_v<T>;

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
    // component of this same type in this same scene -- the same rule the
    // interface underneath states for its pointer, and for the same
    // reason. Take a copy, or ask again.
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

    // Every component of one type, and the entity each belongs to at the
    // same position. Both are empty when the scene holds none.
    //
    // Adding or removing a component of this type makes both stale --
    // storage may have moved and the order certainly has. Finish with them
    // before changing what is in them, or record the change into the
    // command buffer above and let it land afterwards.
    template<ComponentType T>
    std::span<T> View() const
    {
        u32 count = 0;
        void* data = Fluxion_Scene_GetComponentArray(m_handle, Core::TypeIdOf<T>(), &count);
        return std::span<T>(static_cast<T*>(data), count);
    }

    template<ComponentType T>
    std::span<const FluxionGameObjectHandle> Owners() const
    {
        u32 count = 0;
        const FluxionGameObjectHandle* owners = Fluxion_Scene_GetComponentOwners(m_handle, Core::TypeIdOf<T>(), &count);
        return std::span<const FluxionGameObjectHandle>(owners, count);
    }

    // The entity owning the component at this position of View<T>().
    template<ComponentType T>
    Entity OwnerAt(usize index) const
    {
        const std::span<const FluxionGameObjectHandle> owners = Owners<T>();
        if (index >= owners.size()) return Entity();
        return Entity(m_handle, owners[index]);
    }

    template<ComponentType T>
    u32 ComponentCount() const
    {
        return Fluxion_Scene_ComponentCount(m_handle, Core::TypeIdOf<T>());
    }

private:
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
