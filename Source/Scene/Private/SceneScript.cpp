#include <Fluxion/Scene/SceneScript.hpp>

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Log.h>

#include <new>
#include <string>
#include <vector>

using namespace Fluxion::Script;

namespace Fluxion::Scene
{

namespace
{

const char* const kLogChannel = "Scene";

// The names the scene looks for on a component class. Missing is the
// ordinary case and means nothing is called.
const char* const kLifecycleNames[FLUXION_SCENE_LIFECYCLE_COUNT] = {
    "Awake",
    "Start",
    "Update",
    "LateUpdate",
    "OnDestroy",
};

// How many arguments each of them is written to take. Awake, Start and
// OnDestroy take nothing; the two that run every turn are handed how much
// time has passed.
const u32 kLifecycleParameterCount[FLUXION_SCENE_LIFECYCLE_COUNT] = { 0, 0, 1, 1, 0 };

// The class a component is built on, and the one method on it the scene
// itself calls: the object a component belongs to is not something a
// constructor can be given, so it is handed over immediately afterwards.
const char* const kComponentClassName = "Component";
const char* const kComponentBindMethod = "Bind";

EngineHandle ToEngineHandle(FluxionGameObjectHandle object)
{
    EngineHandle handle;
    handle.index = object.index;
    handle.generation = object.generation;
    return handle;
}

FluxionGameObjectHandle ToGameObjectHandle(EngineHandle handle)
{
    FluxionGameObjectHandle object;
    object.index = handle.index;
    object.generation = handle.generation;
    return object;
}

ObjectHandle InstanceOf(const FluxionSceneComponentRecord& component)
{
    ObjectHandle handle;
    handle.index = component.instanceIndex;
    handle.generation = component.instanceGeneration;
    return handle;
}

// --- What a script reaches an object through ----------------------------

// A script never holds a game object, only a handle to one. This is what
// that handle turns into for the length of one call: which scene, and
// which object of it. One of these exists per object record, and the
// resolver writes the pair into it after it has checked that the handle
// still names something -- so a call that reaches a method here has
// already been established to be about a live object.
struct ScriptGameObject
{
    FluxionSceneHandle scene;
    FluxionGameObjectHandle self;

    FluxionScriptString GetName() const { return Fluxion_GameObject_GetName(scene, self); }
    void SetName(FluxionScriptString name) { Fluxion_GameObject_SetName(scene, self, name); }

    bool HasParent() const { return FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetParent(scene, self)); }
    EngineHandle GetParent() const { return ToEngineHandle(Fluxion_GameObject_GetParent(scene, self)); }
    void SetParent(EngineHandle parent) { Fluxion_GameObject_SetParent(scene, self, ToGameObjectHandle(parent)); }
    void DetachFromParent() { Fluxion_GameObject_SetParent(scene, self, Fluxion_GameObject_InvalidHandle()); }

    // The transform is named by the same two numbers the object is: every
    // object has exactly one, so there is nothing else for the pair to
    // mean.
    EngineHandle GetTransform() const { return ToEngineHandle(self); }

    i32 GetChildCount() const { return (i32)Fluxion_GameObject_GetChildCount(scene, self); }
    bool HasChild(FluxionScriptString name) const
    {
        return FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_FindChild(scene, self, name));
    }
    EngineHandle FindChild(FluxionScriptString name) const
    {
        return ToEngineHandle(Fluxion_GameObject_FindChild(scene, self, name));
    }
    EngineHandle FindChildInDescendants(FluxionScriptString name) const
    {
        return ToEngineHandle(Fluxion_GameObject_FindChildRecursive(scene, self, name));
    }

    void Destroy() { Fluxion_GameObject_Destroy(scene, self); }

    // The four below are written with a type rather than with a number:
    // what arrives here is the class index the compiler already knew, so
    // no component is ever looked for by name.
    FluxionScriptObject AddComponent(i32 classIndex)
    {
        return Fluxion::Scene::AddComponent(scene, self, (u32)classIndex);
    }
    FluxionScriptObject GetComponent(i32 classIndex)
    {
        return Fluxion::Scene::GetComponent(scene, self, (u32)classIndex);
    }
    bool HasComponent(i32 classIndex) { return Fluxion::Scene::HasComponent(scene, self, (u32)classIndex); }
    bool RemoveComponent(i32 classIndex) { return Fluxion::Scene::RemoveComponent(scene, self, (u32)classIndex); }
};

// The same arrangement for the transform an object intrinsically has.
// Position, rotation and scale cross as separate numbers rather than as a
// value the language would have to grow a new kind for.
struct ScriptTransform
{
    FluxionSceneHandle scene;
    FluxionGameObjectHandle self;

    void SetLocalPosition(f32 x, f32 y, f32 z)
    {
        FluxionVec3 position = { x, y, z };
        Fluxion_GameObject_SetLocalPosition(scene, self, position);
    }
    f32 GetLocalPositionX() const { return Fluxion_GameObject_GetLocalPosition(scene, self).x; }
    f32 GetLocalPositionY() const { return Fluxion_GameObject_GetLocalPosition(scene, self).y; }
    f32 GetLocalPositionZ() const { return Fluxion_GameObject_GetLocalPosition(scene, self).z; }

    void SetLocalRotation(f32 x, f32 y, f32 z, f32 w)
    {
        FluxionQuat rotation = { x, y, z, w };
        Fluxion_GameObject_SetLocalRotation(scene, self, rotation);
    }
    f32 GetLocalRotationX() const { return Fluxion_GameObject_GetLocalRotation(scene, self).x; }
    f32 GetLocalRotationY() const { return Fluxion_GameObject_GetLocalRotation(scene, self).y; }
    f32 GetLocalRotationZ() const { return Fluxion_GameObject_GetLocalRotation(scene, self).z; }
    f32 GetLocalRotationW() const { return Fluxion_GameObject_GetLocalRotation(scene, self).w; }

    void SetLocalScale(f32 x, f32 y, f32 z)
    {
        FluxionVec3 scale = { x, y, z };
        Fluxion_GameObject_SetLocalScale(scene, self, scale);
    }
    f32 GetLocalScaleX() const { return Fluxion_GameObject_GetLocalScale(scene, self).x; }
    f32 GetLocalScaleY() const { return Fluxion_GameObject_GetLocalScale(scene, self).y; }
    f32 GetLocalScaleZ() const { return Fluxion_GameObject_GetLocalScale(scene, self).z; }

    void Rotate(f32 pitch, f32 yaw, f32 roll)
    {
        FluxionVec3 euler = { pitch, yaw, roll };
        Fluxion_GameObject_Rotate(scene, self, euler);
    }
};

bool EnsureViews(FluxionSceneRecord* record)
{
    if (record->gameObjectViews != nullptr && record->transformViews != nullptr) return true;

    // Built without exceptions, so a failed allocation arrives as null
    // and is reported like any other refusal.
    auto* objects = new (std::nothrow) ScriptGameObject[FLUXION_SCENE_MAX_GAME_OBJECTS];
    auto* transforms = new (std::nothrow) ScriptTransform[FLUXION_SCENE_MAX_GAME_OBJECTS];
    if (!objects || !transforms)
    {
        delete[] objects;
        delete[] transforms;
        Fluxion_SceneInternal_SetError(record, "there was not enough memory to make this scene reachable from a script");
        return false;
    }

    record->gameObjectViews = objects;
    record->transformViews = transforms;
    return true;
}

void* ResolveGameObject(void* user, EngineHandle handle)
{
    auto* record = (FluxionSceneRecord*)user;
    const FluxionGameObjectHandle object = ToGameObjectHandle(handle);
    if (!Fluxion_SceneInternal_ResolveObject(record, object)) return nullptr;
    if (!record->gameObjectViews) return nullptr;

    auto* views = (ScriptGameObject*)record->gameObjectViews;
    views[handle.index].scene = record->self;
    views[handle.index].self = object;
    return &views[handle.index];
}

void* ResolveTransform(void* user, EngineHandle handle)
{
    auto* record = (FluxionSceneRecord*)user;
    const FluxionGameObjectHandle object = ToGameObjectHandle(handle);
    if (!Fluxion_SceneInternal_ResolveObject(record, object)) return nullptr;
    if (!record->transformViews) return nullptr;

    auto* views = (ScriptTransform*)record->transformViews;
    views[handle.index].scene = record->self;
    views[handle.index].self = object;
    return &views[handle.index];
}

// --- Describing the two types to the binding table ----------------------

// Owns every array the descriptors point at for as long as the table is
// being built, which is all the binding table needs: it copies what it
// keeps.
class SurfaceDescriptors
{
public:
    SurfaceDescriptors()
    {
        m_string[0] = ScriptStringTypeId();
        m_gameObject[0] = FLUXION_TYPE_ID_OF(FluxionGameObject);
        m_classArgument[0] = FLUXION_TYPE_ID_OF(i32);
        m_threeFloats[0] = FLUXION_TYPE_ID_OF(f32);
        m_threeFloats[1] = FLUXION_TYPE_ID_OF(f32);
        m_threeFloats[2] = FLUXION_TYPE_ID_OF(f32);
        m_fourFloats[0] = FLUXION_TYPE_ID_OF(f32);
        m_fourFloats[1] = FLUXION_TYPE_ID_OF(f32);
        m_fourFloats[2] = FLUXION_TYPE_ID_OF(f32);
        m_fourFloats[3] = FLUXION_TYPE_ID_OF(f32);

        const FluxionTypeId stringType = ScriptStringTypeId();
        const FluxionTypeId objectType = ScriptObjectTypeId();
        const FluxionTypeId gameObjectType = FLUXION_TYPE_ID_OF(FluxionGameObject);
        const FluxionTypeId transformType = FLUXION_TYPE_ID_OF(FluxionTransform);

        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::GetName>("GetName", stringType));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::SetName>("SetName", FLUXION_TYPE_ID_INVALID, m_string));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::HasParent>("HasParent", FLUXION_TYPE_ID_OF(bool)));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::GetParent>("GetParent", gameObjectType));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::SetParent>("SetParent", FLUXION_TYPE_ID_INVALID, m_gameObject));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::DetachFromParent>("DetachFromParent", FLUXION_TYPE_ID_INVALID));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::GetTransform>("GetTransform", transformType));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::GetChildCount>("GetChildCount", FLUXION_TYPE_ID_OF(i32)));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::HasChild>("HasChild", FLUXION_TYPE_ID_OF(bool), m_string));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::FindChild>("FindChild", gameObjectType, m_string));
        m_gameObjectMethods.push_back(
            Core::ReflectMethod<&ScriptGameObject::FindChildInDescendants>("FindChildInDescendants", gameObjectType, m_string));
        m_gameObjectMethods.push_back(Core::ReflectMethod<&ScriptGameObject::Destroy>("Destroy", FLUXION_TYPE_ID_INVALID));

        AddTypeArgumentMethod(Core::ReflectMethod<&ScriptGameObject::AddComponent>("AddComponent", objectType, m_classArgument));
        AddTypeArgumentMethod(Core::ReflectMethod<&ScriptGameObject::GetComponent>("GetComponent", objectType, m_classArgument));
        AddTypeArgumentMethod(
            Core::ReflectMethod<&ScriptGameObject::HasComponent>("HasComponent", FLUXION_TYPE_ID_OF(bool), m_classArgument));
        AddTypeArgumentMethod(
            Core::ReflectMethod<&ScriptGameObject::RemoveComponent>("RemoveComponent", FLUXION_TYPE_ID_OF(bool), m_classArgument));

        const FluxionTypeId floatType = FLUXION_TYPE_ID_OF(f32);
        m_transformMethods.push_back(
            Core::ReflectMethod<&ScriptTransform::SetLocalPosition>("SetLocalPosition", FLUXION_TYPE_ID_INVALID, m_threeFloats));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalPositionX>("GetLocalPositionX", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalPositionY>("GetLocalPositionY", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalPositionZ>("GetLocalPositionZ", floatType));
        m_transformMethods.push_back(
            Core::ReflectMethod<&ScriptTransform::SetLocalRotation>("SetLocalRotation", FLUXION_TYPE_ID_INVALID, m_fourFloats));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalRotationX>("GetLocalRotationX", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalRotationY>("GetLocalRotationY", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalRotationZ>("GetLocalRotationZ", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalRotationW>("GetLocalRotationW", floatType));
        m_transformMethods.push_back(
            Core::ReflectMethod<&ScriptTransform::SetLocalScale>("SetLocalScale", FLUXION_TYPE_ID_INVALID, m_threeFloats));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalScaleX>("GetLocalScaleX", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalScaleY>("GetLocalScaleY", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::GetLocalScaleZ>("GetLocalScaleZ", floatType));
        m_transformMethods.push_back(Core::ReflectMethod<&ScriptTransform::Rotate>("Rotate", FLUXION_TYPE_ID_INVALID, m_threeFloats));

        // The names the script sees are the plain ones; the identities
        // behind them are this module's own, so nothing else can claim
        // them.
        m_gameObjectType.name = Fluxion_StringView_FromCStr("GameObject");
        m_gameObjectType.id = gameObjectType;
        m_gameObjectType.kind = FLUXION_TYPE_KIND_STRUCT;
        m_gameObjectType.size = sizeof(ScriptGameObject);
        m_gameObjectType.version = 1;
        m_gameObjectType.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        m_gameObjectType.methods = Fluxion_Span_Make(m_gameObjectMethods.data(), m_gameObjectMethods.size(), sizeof(FluxionMethodInfo));

        m_transformType.name = Fluxion_StringView_FromCStr("Transform");
        m_transformType.id = transformType;
        m_transformType.kind = FLUXION_TYPE_KIND_STRUCT;
        m_transformType.size = sizeof(ScriptTransform);
        m_transformType.version = 1;
        m_transformType.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        m_transformType.methods = Fluxion_Span_Make(m_transformMethods.data(), m_transformMethods.size(), sizeof(FluxionMethodInfo));
    }

    SurfaceDescriptors(const SurfaceDescriptors&) = delete;
    SurfaceDescriptors& operator=(const SurfaceDescriptors&) = delete;

    const FluxionTypeInfo& GameObjectType() const { return m_gameObjectType; }
    const FluxionTypeInfo& TransformType() const { return m_transformType; }

private:
    // ReflectMethod reads the flags off the function pointer, which can
    // say whether an instance is needed and nothing more. That the first
    // argument is a type rather than a number is a decision about the
    // interface, so it is written here.
    void AddTypeArgumentMethod(FluxionMethodInfo method)
    {
        method.flags |= FLUXION_METHOD_FLAG_SCRIPT_TYPE_ARGUMENT;
        m_gameObjectMethods.push_back(method);
    }

    FluxionTypeId m_string[1]{};
    FluxionTypeId m_gameObject[1]{};
    FluxionTypeId m_classArgument[1]{};
    FluxionTypeId m_threeFloats[3]{};
    FluxionTypeId m_fourFloats[4]{};

    std::vector<FluxionMethodInfo> m_gameObjectMethods;
    std::vector<FluxionMethodInfo> m_transformMethods;

    FluxionTypeInfo m_gameObjectType{};
    FluxionTypeInfo m_transformType{};
};

// --- Components ---------------------------------------------------------

Vm* MachineOf(FluxionSceneRecord* record) { return (Vm*)record->vm; }

void ReportFault(FluxionSceneRecord* record, const char* what, const char* className)
{
    Vm* vm = MachineOf(record);
    const FaultDetail& detail = GetFaultDetail(vm);
    const std::string message = std::string(what) + " on '" + (className ? className : "<unknown>") + "': " +
                                (detail.message ? detail.message : "the script stopped without saying why");
    Fluxion_SceneInternal_SetError(record, message.c_str());
    FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
}

const char* ClassNameOf(FluxionSceneRecord* record, u32 classIndex)
{
    const char* name = ClassName(MachineOf(record), classIndex);
    return name ? name : "<unknown>";
}

// Runs one lifecycle method, if the class declared it. A fault is
// reported and the component is left where it is: taking it away because
// its Update went wrong would hide the next one.
void CallLifecycle(FluxionSceneRecord* record, FluxionSceneComponentRecord& component, FluxionSceneLifecycle which, f32 deltaTime)
{
    const u32 method = component.methods[which];
    if (method == FLUXION_SCENE_NO_FUNCTION) return;

    ScriptValue argument;
    argument.type = ValueType::Float;
    argument.floatValue = deltaTime;

    const u32 argumentCount = kLifecycleParameterCount[which];
    auto result = InvokeMethod(MachineOf(record), InstanceOf(component), method, argumentCount != 0 ? &argument : nullptr, argumentCount);
    if (!result.IsOk()) ReportFault(record, kLifecycleNames[which], ClassNameOf(record, component.classIndex));
}

void DetachRecord(FluxionSceneRecord* record, u32 componentIndex)
{
    FluxionSceneComponentRecord& component = record->components[componentIndex];

    FluxionSceneGameObjectRecord* owner = Fluxion_SceneInternal_ResolveObject(record, component.owner);
    if (owner != nullptr)
    {
        u32* link = &owner->firstComponent;
        while (*link != FLUXION_SCENE_NO_COMPONENT)
        {
            if (*link == componentIndex)
            {
                *link = component.nextOnOwner;
                break;
            }
            link = &record->components[*link].nextOnOwner;
        }
    }

    // The scene was the only thing holding this object up. Everything
    // else the script can still see it through keeps it alive on its own
    // account; nothing else does, and it goes at the next collection.
    UnpinObject(MachineOf(record), InstanceOf(component));

    component.inUse = false;
    component.removing = false;
    component.nextOnOwner = FLUXION_SCENE_NO_COMPONENT;
}

u32 FindComponentIndex(FluxionSceneRecord* record, FluxionGameObjectHandle object, u32 classIndex)
{
    FluxionSceneGameObjectRecord* owner = Fluxion_SceneInternal_ResolveObject(record, object);
    if (owner == nullptr) return FLUXION_SCENE_NO_COMPONENT;

    u32 cursor = owner->firstComponent;
    while (cursor != FLUXION_SCENE_NO_COMPONENT)
    {
        const FluxionSceneComponentRecord& component = record->components[cursor];
        if (component.inUse && !component.removing && component.classIndex == classIndex) return cursor;
        cursor = component.nextOnOwner;
    }
    return FLUXION_SCENE_NO_COMPONENT;
}

// One pass over every component that is going: tell it, let go of it, and
// free the record. Repeated until nothing new was marked, because being
// told is script code and script code may ask for more.
void FlushRemovals(FluxionSceneRecord* record)
{
    if (record->flushing) return;

    record->flushing = true;
    const bool wasDispatching = record->dispatching;
    record->dispatching = true;

    bool any = true;
    while (any)
    {
        any = false;
        for (u32 i = 0; i < record->componentHighWater; ++i)
        {
            FluxionSceneComponentRecord& component = record->components[i];
            if (!component.inUse || !component.removing) continue;

            CallLifecycle(record, component, FLUXION_SCENE_LIFECYCLE_ON_DESTROY, 0.0f);
            DetachRecord(record, i);
            any = true;
        }
    }

    record->dispatching = wasDispatching;
    record->flushing = false;

    Fluxion_SceneInternal_FreePendingObjects(record);
}

void MarkComponentsOf(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == nullptr) return;

    u32 cursor = entry->firstComponent;
    while (cursor != FLUXION_SCENE_NO_COMPONENT)
    {
        record->components[cursor].removing = true;
        cursor = record->components[cursor].nextOnOwner;
    }

    FluxionGameObjectHandle child = entry->firstChild;
    while (FLUXION_HANDLE_IS_VALID(child))
    {
        FluxionSceneGameObjectRecord* childEntry = Fluxion_SceneInternal_ResolveObject(record, child);
        if (childEntry == nullptr) break;
        MarkComponentsOf(record, child);
        child = childEntry->nextSibling;
    }
}

// One step of a turn: the same walk each time, over the records that
// existed when the turn began.
void DispatchStep(FluxionSceneRecord* record, FluxionSceneLifecycle which, u32 limit, f32 deltaTime, bool onceOnly)
{
    record->dispatching = true;
    for (u32 i = 0; i < limit; ++i)
    {
        FluxionSceneComponentRecord& component = record->components[i];
        if (!component.inUse || component.removing) continue;

        if (onceOnly)
        {
            bool* pending = (which == FLUXION_SCENE_LIFECYCLE_AWAKE) ? &component.awakePending : &component.startPending;
            if (!*pending) continue;
            *pending = false;
        }

        CallLifecycle(record, component, which, deltaTime);
    }
    record->dispatching = false;

    FlushRemovals(record);
}

} // namespace

// --- The public C++ surface ---------------------------------------------

const char* ComponentPreludeSource()
{
    // The object a component belongs to and that object's transform are
    // handed over immediately after the instance is made, because a
    // constructor is written by whoever wrote the component and cannot be
    // relied on to take them.
    return
        "class Component\n"
        "{\n"
        "    GameObject gameObject;\n"
        "    Transform transform;\n"
        "\n"
        "    void Bind(GameObject owner, Transform ownerTransform)\n"
        "    {\n"
        "        this.gameObject = owner;\n"
        "        this.transform = ownerTransform;\n"
        "    }\n"
        "}\n";
}

bool BuildBindingTable(FluxionSceneHandle scene, BindingTable& table, DiagnosticList& outDiagnostics)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr)
    {
        outDiagnostics.AddError(SourceLocation{ "<scene>", 0, 0 }, "there is no such scene to make reachable from a script");
        return false;
    }
    if (!EnsureViews(record)) return false;

    // The transform goes in first: a type may only take or answer with a
    // handle to something the table already holds, and the game object
    // answers with a transform while the transform mentions nothing.
    SurfaceDescriptors descriptors;
    const u32 transformIndex = AddBoundType(table, descriptors.TransformType(), &ResolveTransform, record, outDiagnostics);
    const u32 gameObjectIndex = AddBoundType(table, descriptors.GameObjectType(), &ResolveGameObject, record, outDiagnostics);
    return gameObjectIndex != kNoBoundType && transformIndex != kNoBoundType;
}

bool AttachRuntime(FluxionSceneHandle scene, Vm* vm)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr) return false;

    if (vm == nullptr)
    {
        record->vm = nullptr;
        record->componentClass = kNoClass;
        record->bindMethod = FLUXION_SCENE_NO_FUNCTION;
        return true;
    }
    if (!EnsureViews(record)) return false;

    const u32 componentClass = FindClass(vm, kComponentClassName);
    if (componentClass == kNoClass)
    {
        Fluxion_SceneInternal_SetError(record, "this script module has no class for components to be built on");
        return false;
    }

    const u32 bindMethod = FindMethod(vm, componentClass, kComponentBindMethod);
    if (bindMethod == kNoFunction)
    {
        Fluxion_SceneInternal_SetError(record, "the class components are built on has no way of being handed the object it belongs to");
        return false;
    }

    record->vm = vm;
    record->componentClass = componentClass;
    record->bindMethod = bindMethod;
    Fluxion_SceneInternal_SetError(record, nullptr);
    return true;
}

Vm* GetRuntime(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != nullptr ? MachineOf(record) : nullptr;
}

u32 FindComponentClass(FluxionSceneHandle scene, const char* className)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr || record->vm == nullptr) return kNoClass;
    return FindClass(MachineOf(record), className);
}

ObjectHandle AddComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex)
{
    const ObjectHandle none;

    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr) return none;

    Vm* vm = MachineOf(record);
    if (vm == nullptr)
    {
        Fluxion_SceneInternal_SetError(record, "this scene has no script machine to make a component on");
        return none;
    }

    FluxionSceneGameObjectRecord* owner = Fluxion_SceneInternal_ResolveObject(record, object);
    if (owner == nullptr || owner->pendingDestroy)
    {
        Fluxion_SceneInternal_SetError(record, "there is no such object to put a component on");
        return none;
    }

    if (!ClassDerivesFrom(vm, classIndex, record->componentClass))
    {
        const std::string message = std::string("'") + ClassNameOf(record, classIndex) + "' is not built on '" + kComponentClassName +
                                    "', so it is not something that can be attached";
        Fluxion_SceneInternal_SetError(record, message.c_str());
        FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
        return none;
    }

    // What the class says it needs has to already be there. Attaching it
    // for the caller would mean deciding, on the caller's behalf, in what
    // state the thing it needs should arrive -- so this refuses instead,
    // and says exactly what is missing.
    for (u32 i = 0; i < ClassAttributeCount(vm, classIndex); ++i)
    {
        const Attribute* attribute = ClassAttributeAt(vm, classIndex, i);
        if (!attribute || attribute->name != "RequireComponent" || attribute->arguments.empty()) continue;

        const u32 required = attribute->arguments[0].classIndex;
        if (FindComponentIndex(record, object, required) != FLUXION_SCENE_NO_COMPONENT) continue;

        const std::string message = std::string("'") + ClassNameOf(record, classIndex) + "' requires '" + ClassNameOf(record, required) +
                                    "', which '" + Fluxion_GameObject_GetName(scene, object) + "' does not have";
        Fluxion_SceneInternal_SetError(record, message.c_str());
        FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
        return none;
    }

    u32 slot = FLUXION_SCENE_MAX_COMPONENTS;
    for (u32 i = 0; i < FLUXION_SCENE_MAX_COMPONENTS; ++i)
    {
        if (!record->components[i].inUse) { slot = i; break; }
    }
    if (slot == FLUXION_SCENE_MAX_COMPONENTS)
    {
        Fluxion_SceneInternal_SetError(record, "this scene already holds as many components as one may hold");
        return none;
    }

    // Every lifecycle method is looked up once, here, so a turn of the
    // scene never compares a name against anything.
    u32 methods[FLUXION_SCENE_LIFECYCLE_COUNT];
    for (u32 i = 0; i < FLUXION_SCENE_LIFECYCLE_COUNT; ++i)
    {
        methods[i] = FindMethod(vm, classIndex, kLifecycleNames[i]);
        if (methods[i] == kNoFunction)
        {
            methods[i] = FLUXION_SCENE_NO_FUNCTION;
            continue;
        }

        const u32 declared = MethodParameterCount(vm, methods[i]);
        const bool sameShape = declared == kLifecycleParameterCount[i] &&
                               (declared == 0 || MethodParameterType(vm, methods[i], 0) == ValueType::Float);
        if (sameShape) continue;

        const std::string message = std::string("'") + ClassNameOf(record, classIndex) + "." + kLifecycleNames[i] + "' has to take " +
                                    (kLifecycleParameterCount[i] == 0 ? "nothing" : "how much time has passed, as a float");
        Fluxion_SceneInternal_SetError(record, message.c_str());
        FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
        return none;
    }

    auto created = NewInstance(vm, classIndex, nullptr, 0);
    if (!created.IsOk())
    {
        ReportFault(record, "making", ClassNameOf(record, classIndex));
        return none;
    }

    const ObjectHandle instance = created.Value();

    // From here on the scene is what holds this object up. Nothing the
    // script can see refers to it, so without this the very next
    // collection would take a component the scene is still going to call
    // into every turn.
    if (!PinObject(vm, instance))
    {
        Fluxion_SceneInternal_SetError(record, "the component that was made could not be held onto");
        return none;
    }

    ScriptValue arguments[2];
    arguments[0].type = ValueType::Handle;
    arguments[0].handleValue = ToEngineHandle(object);
    arguments[1].type = ValueType::Handle;
    arguments[1].handleValue = ToEngineHandle(object);

    auto bound = InvokeMethod(vm, instance, record->bindMethod, arguments, 2);
    if (!bound.IsOk())
    {
        ReportFault(record, "handing its object to", ClassNameOf(record, classIndex));
        UnpinObject(vm, instance);
        return none;
    }

    FluxionSceneComponentRecord& component = record->components[slot];
    component.inUse = true;
    component.removing = false;
    component.awakePending = true;
    component.startPending = true;
    component.owner = object;
    component.classIndex = classIndex;
    component.instanceIndex = instance.index;
    component.instanceGeneration = instance.generation;
    for (u32 i = 0; i < FLUXION_SCENE_LIFECYCLE_COUNT; ++i) component.methods[i] = methods[i];

    component.nextOnOwner = FLUXION_SCENE_NO_COMPONENT;
    if (owner->firstComponent == FLUXION_SCENE_NO_COMPONENT)
    {
        owner->firstComponent = slot;
    }
    else
    {
        u32 cursor = owner->firstComponent;
        while (record->components[cursor].nextOnOwner != FLUXION_SCENE_NO_COMPONENT) cursor = record->components[cursor].nextOnOwner;
        record->components[cursor].nextOnOwner = slot;
    }

    if (slot + 1 > record->componentHighWater) record->componentHighWater = slot + 1;
    return instance;
}

ObjectHandle GetComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    const u32 found = FindComponentIndex(record, object, classIndex);
    if (found == FLUXION_SCENE_NO_COMPONENT) return ObjectHandle{};
    return InstanceOf(record->components[found]);
}

bool HasComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return FindComponentIndex(record, object, classIndex) != FLUXION_SCENE_NO_COMPONENT;
}

bool RemoveComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    const u32 found = FindComponentIndex(record, object, classIndex);
    if (found == FLUXION_SCENE_NO_COMPONENT) return false;

    record->components[found].removing = true;
    if (!record->dispatching) FlushRemovals(record);
    return true;
}

u32 ComponentCount(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* owner = Fluxion_SceneInternal_ResolveObject(record, object);
    if (owner == nullptr) return 0;

    u32 count = 0;
    u32 cursor = owner->firstComponent;
    while (cursor != FLUXION_SCENE_NO_COMPONENT)
    {
        if (record->components[cursor].inUse && !record->components[cursor].removing) ++count;
        cursor = record->components[cursor].nextOnOwner;
    }
    return count;
}

} // namespace Fluxion::Scene

// --- What the C half calls -----------------------------------------------

extern "C" void Fluxion_SceneComponents_MarkSubtree(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    if (record == nullptr) return;
    Fluxion::Scene::MarkComponentsOf(record, object);
}

extern "C" void Fluxion_SceneComponents_Flush(FluxionSceneRecord* record)
{
    if (record == nullptr) return;
    Fluxion::Scene::FlushRemovals(record);
}

extern "C" void Fluxion_SceneComponents_ReleaseScene(FluxionSceneRecord* record)
{
    if (record == nullptr) return;

    delete[] (Fluxion::Scene::ScriptGameObject*)record->gameObjectViews;
    delete[] (Fluxion::Scene::ScriptTransform*)record->transformViews;
    record->gameObjectViews = nullptr;
    record->transformViews = nullptr;
    record->vm = nullptr;
}

extern "C" void Fluxion_Scene_Tick(FluxionSceneHandle scene, f32 deltaTime)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr || record->vm == nullptr) return;

    // What the table held when the turn began. A component attached while
    // this runs lands above the line and is picked up next turn, which is
    // what keeps one turn from calling a component into being and then
    // straight away calling into it.
    const u32 limit = record->componentHighWater;

    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_AWAKE, limit, deltaTime, true);
    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_START, limit, deltaTime, true);
    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_UPDATE, limit, deltaTime, false);
    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_LATE_UPDATE, limit, deltaTime, false);
}
