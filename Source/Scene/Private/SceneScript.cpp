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

// The head of an object's list of scripts. It lives in a component of the
// object's own rather than on the object record, so there is one place it
// can be, and so the storage can be asked which objects have scripts.
u32* ScriptListHead(FluxionSceneRecord* record, FluxionSceneGameObjectRecord* entry)
{
    FluxionScriptComponent* link =
        (FluxionScriptComponent*)Fluxion_SceneArchetype_ValueOf(record, entry, Fluxion_ScriptComponent_TypeId());
    return (link != nullptr) ? &link->firstComponent : nullptr;
}

u32 FindComponentIndex(FluxionSceneRecord* record, FluxionGameObjectHandle object, u32 classIndex)
{
    FluxionSceneGameObjectRecord* owner = Fluxion_SceneInternal_ResolveObject(record, object);
    if (owner == nullptr) return FLUXION_SCENE_NO_COMPONENT;

    const u32* head = ScriptListHead(record, owner);
    if (head == nullptr) return FLUXION_SCENE_NO_COMPONENT;

    u32 cursor = *head;
    while (cursor != FLUXION_SCENE_NO_COMPONENT)
    {
        const FluxionSceneComponentRecord& component = record->components[cursor];
        if (component.inUse && !component.removing && component.classIndex == classIndex) return cursor;
        cursor = component.nextOnOwner;
    }
    return FLUXION_SCENE_NO_COMPONENT;
}

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
        u32* link = ScriptListHead(record, owner);
        if (link == nullptr) return;
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

    const u32* head = ScriptListHead(record, entry);
    if (head == nullptr) return;

    u32 cursor = *head;
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

// Every lifecycle method the class declares, looked up once so a turn of
// the scene never compares a name against anything. False, with
// `outProblem` saying which one and what is wrong with it, when a class
// declares one of them with a shape the scene cannot call.
bool ResolveLifecycle(Vm* vm, u32 classIndex, const char* className, u32* outMethods, std::string& outProblem)
{
    for (u32 i = 0; i < FLUXION_SCENE_LIFECYCLE_COUNT; ++i)
    {
        outMethods[i] = FindMethod(vm, classIndex, kLifecycleNames[i]);
        if (outMethods[i] == kNoFunction)
        {
            outMethods[i] = FLUXION_SCENE_NO_FUNCTION;
            continue;
        }

        const u32 declared = MethodParameterCount(vm, outMethods[i]);
        const bool sameShape = declared == kLifecycleParameterCount[i] &&
                               (declared == 0 || MethodParameterType(vm, outMethods[i], 0) == ValueType::Float);
        if (sameShape) continue;

        outProblem = std::string("'") + className + "." + kLifecycleNames[i] + "' has to take " +
                     (kLifecycleParameterCount[i] == 0 ? "nothing" : "how much time has passed, as a float");
        return false;
    }
    return true;
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

// --- Carrying a component's state from one machine to another -----------
//
// Nothing below ever keeps a class index or a method index across the
// swap. They are handed out in the order declarations are added, so a
// single class written at the top of a file moves every index after it:
// the only thing about a declaration that survives an edit is its name,
// and names are what all of this is matched on.

struct CarriedField
{
    std::string name;
    ValueType type = ValueType::Void;

    // For a reference, a value type or a named set: the declaration the
    // field's type names, written out. For an engine handle it is empty
    // and `boundType` is what says which of the host's types it names --
    // that number comes from the host's own table, which is the same table
    // on both sides of a reload and so does keep its meaning.
    std::string typeName;
    u32 boundType = kNoBoundType;

    // One or the other: a value type travels as the run of slots it
    // occupies, everything else as a single value.
    ScriptValue value;
    std::vector<Slot> slots;
};

struct CarriedComponent
{
    // Which record this was, so the same record is the one written back.
    u32 record = 0;

    std::string className;
    bool awakePending = false;
    bool startPending = false;

    std::vector<CarriedField> fields;

    // Counted here rather than reported as it happens, so a reload that
    // is refused later says nothing about fields it was never going to
    // write.
    u32 dropped = 0;

    // What the new machine turned out to have, filled in by the step that
    // checks the new code still has somewhere to put all of this.
    u32 newClass = kNoClass;
    u32 newMethods[FLUXION_SCENE_LIFECYCLE_COUNT] = {};
    ObjectHandle newInstance;
};

std::string TypeNameOfField(Vm* vm, const FieldInfo& field)
{
    const bool namesOwnClass =
        field.type == ValueType::Object || field.type == ValueType::Struct || field.type == ValueType::Enum;
    if (!namesOwnClass || field.typeClass == kNoClass) return std::string();

    const char* name = ClassName(vm, field.typeClass);
    return name != nullptr ? std::string(name) : std::string();
}

// Every field a component of this class declared for itself, its own base
// classes included, stopping short of the class every component is built
// on. That one holds the object a component belongs to and that object's
// transform, both of which are handed over afresh straight after the new
// instance is made -- carrying them would be writing the same two values
// twice.
template<typename Visitor>
void ForEachOwnField(Vm* vm, u32 classIndex, u32 stopAt, Visitor&& visit)
{
    u32 current = classIndex;
    while (current != kNoClass && current != stopAt)
    {
        const u32 count = ClassFieldCount(vm, current);
        for (u32 i = 0; i < count; ++i)
        {
            const FieldInfo* field = ClassFieldAt(vm, current, i);
            if (field != nullptr) visit(*field);
        }
        current = ClassBaseClass(vm, current);
    }
}

// A field of this name anywhere in the chain, nearest declaration first.
// The language does not let a derived class re-declare a name a base
// already used, so at most one can ever answer.
const FieldInfo* FindFieldInChain(Vm* vm, u32 classIndex, u32 stopAt, const std::string& name)
{
    u32 current = classIndex;
    while (current != kNoClass && current != stopAt)
    {
        if (const FieldInfo* found = FindClassField(vm, current, name.c_str())) return found;
        current = ClassBaseClass(vm, current);
    }
    return nullptr;
}

void SaveComponent(Vm* vm, const FluxionSceneComponentRecord& component, u32 componentClass, CarriedComponent& out)
{
    const char* className = ClassName(vm, component.classIndex);
    out.className = className != nullptr ? className : std::string();
    out.awakePending = component.awakePending;
    out.startPending = component.startPending;

    const ObjectHandle instance = InstanceOf(component);

    ForEachOwnField(vm, component.classIndex, componentClass, [&](const FieldInfo& field) {
        // A field holding a reference is left behind. What it names is an
        // object of the machine being stood down, and there is nothing in
        // the machine taking over that is that object -- bringing it
        // across would mean rebuilding everything reachable from it
        // against a different set of classes, which this does not do.
        if (field.type == ValueType::Object || field.type == ValueType::Null)
        {
            ++out.dropped;
            return;
        }

        CarriedField carried;
        carried.name = field.name;
        carried.type = field.type;
        carried.typeName = TypeNameOfField(vm, field);
        carried.boundType = field.type == ValueType::Handle ? field.typeClass : kNoBoundType;

        if (field.type == ValueType::Struct)
        {
            // A value type may hold only numbers, truth values, named
            // constants and other value types, so its slots are
            // self-contained: what is read out of one machine can be
            // written straight into another.
            const u32 width = FieldSlotCount(vm, field);
            carried.slots.resize(width);
            if (width == 0 || !ReadInstanceFieldSlots(vm, instance, field, carried.slots.data(), width))
            {
                ++out.dropped;
                return;
            }
        }
        else if (!ReadInstanceField(vm, instance, field, carried.value))
        {
            ++out.dropped;
            return;
        }

        out.fields.push_back(std::move(carried));
    });
}

// Writes back everything that still has somewhere to go. A field that is
// gone, renamed, or now declared with a different type is left at whatever
// the new class starts it at and counted, rather than forced into a slot
// that no longer means what it meant.
void RestoreComponent(Vm* vm, CarriedComponent& carried, u32 componentClass, ReloadReport& report)
{
    for (const CarriedField& field : carried.fields)
    {
        const FieldInfo* target = FindFieldInChain(vm, carried.newClass, componentClass, field.name);

        const char* why = nullptr;
        if (target == nullptr) why = "the new code does not declare it";
        else if (target->type != field.type) why = "it is declared with a different type now";
        else if (TypeNameOfField(vm, *target) != field.typeName) why = "the type it is declared with is a different one now";
        else if (field.type == ValueType::Handle && target->typeClass != field.boundType)
            why = "it names a different engine type now";

        if (why == nullptr)
        {
            const bool written = field.type == ValueType::Struct
                ? WriteInstanceFieldSlots(vm, carried.newInstance, *target, field.slots.data(), (u32)field.slots.size())
                : WriteInstanceField(vm, carried.newInstance, *target, field.value);
            if (written)
            {
                ++report.fieldsCarried;
                continue;
            }
            why = "it does not hold the same shape of value now";
        }

        ++report.fieldsDropped;
        FLUXION_LOG_WARN(kLogChannel, "'%s.%s' was not carried across the reload: %s", carried.className.c_str(),
            field.name.c_str(), why);
    }

    report.fieldsDropped += carried.dropped;
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
        ReleaseScriptReflection(record);
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

    // What the machine's classes hold, said in the terms the rest of the
    // engine reads types in -- so that writing a script component out, or
    // showing it, needs no knowledge of the scripting runtime at all.
    PublishScriptReflection(record);

    Fluxion_SceneInternal_SetError(record, nullptr);
    return true;
}

Vm* GetRuntime(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != nullptr ? MachineOf(record) : nullptr;
}

bool ReloadRuntime(FluxionSceneHandle scene, const ReloadRequest& request, DiagnosticList& outDiagnostics, ReloadReport& outReport)
{
    outReport = ReloadReport{};

    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr) return false;

    Vm* oldVm = MachineOf(record);
    if (oldVm == nullptr)
    {
        Fluxion_SceneInternal_SetError(record, "this scene has no script machine to put new code under");
        return false;
    }

    // A component that was already going is finished with first: it has no
    // state worth carrying and it would otherwise be rebuilt only to be
    // taken apart again on the next turn.
    FlushRemovals(record);

    // --- Nothing below this point disturbs anything until it has all
    // succeeded. The scene keeps running what it is running.

    CompileCacheReport cacheReport;
    auto compiled = CompileCached(request.source, request.options, request.cache, outDiagnostics, cacheReport);
    if (!compiled.IsOk())
    {
        Fluxion_SceneInternal_SetError(record, "the new script source did not compile, so the scene is still running the old one");
        return false;
    }

    Vm* newVm = CreateVm(compiled.Value(), outDiagnostics, request.options.bindings);
    if (newVm == nullptr)
    {
        Fluxion_SceneInternal_SetError(record, "the new script module did not load, so the scene is still running the old one");
        return false;
    }

    // Everything the old machine was printing to goes on being printed to.
    // Instances are made below, and a constructor may well write
    // something, so this is put in place before any of that happens.
    void* outputUser = nullptr;
    OutputHandler outputHandler = GetOutputHandler(oldVm, &outputUser);
    if (outputHandler != nullptr) SetOutputHandler(newVm, outputHandler, outputUser);

    struct MachineGuard
    {
        Vm* machine = nullptr;
        ~MachineGuard() { DestroyVm(machine); }
        void Keep() { machine = nullptr; }
    } guard{ newVm };

    const u32 newComponentClass = FindClass(newVm, kComponentClassName);
    const u32 newBindMethod =
        newComponentClass != kNoClass ? FindMethod(newVm, newComponentClass, kComponentBindMethod) : kNoFunction;
    if (newComponentClass == kNoClass || newBindMethod == kNoFunction)
    {
        Fluxion_SceneInternal_SetError(record,
            "the new script module has no class for components to be built on, so the scene is still running the old one");
        return false;
    }

    // --- What the scene is holding, in terms that outlive the machine ---

    std::vector<CarriedComponent> carried;
    for (u32 i = 0; i < record->componentHighWater; ++i)
    {
        const FluxionSceneComponentRecord& component = record->components[i];
        if (!component.inUse || component.removing) continue;

        CarriedComponent entry;
        entry.record = i;
        SaveComponent(oldVm, component, record->componentClass, entry);
        carried.push_back(std::move(entry));
    }

    // --- Does the new code still have somewhere to put all of it? -------

    for (CarriedComponent& entry : carried)
    {
        entry.newClass = FindClass(newVm, entry.className.c_str());
        if (entry.newClass == kNoClass || !ClassDerivesFrom(newVm, entry.newClass, newComponentClass))
        {
            const std::string message = std::string("the new script source has no component called '") + entry.className +
                                        "', which the scene has attached, so the scene is still running the old one";
            Fluxion_SceneInternal_SetError(record, message.c_str());
            FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
            return false;
        }

        std::string problem;
        if (!ResolveLifecycle(newVm, entry.newClass, entry.className.c_str(), entry.newMethods, problem))
        {
            const std::string message = problem + ", so the scene is still running the old one";
            Fluxion_SceneInternal_SetError(record, message.c_str());
            FLUXION_LOG_ERROR(kLogChannel, "%s", message.c_str());
            return false;
        }
    }

    // --- Building the replacements ---------------------------------------
    //
    // The scene is given the new machine before any of this runs, because
    // a constructor is script code and script code may reach back into the
    // scene: whatever it reaches has to be the machine its own class came
    // out of. The old records are left exactly as they are, so putting the
    // old machine back is all it takes to undo this.

    void* const oldVmPointer = record->vm;
    const u32 oldComponentClass = record->componentClass;
    const u32 oldBindMethod = record->bindMethod;

    record->vm = newVm;
    record->componentClass = newComponentClass;
    record->bindMethod = newBindMethod;

    bool built = true;
    for (CarriedComponent& entry : carried)
    {
        auto created = NewInstance(newVm, entry.newClass, nullptr, 0);
        if (!created.IsOk())
        {
            ReportFault(record, "making", entry.className.c_str());
            built = false;
            break;
        }
        entry.newInstance = created.Value();

        if (!PinObject(newVm, entry.newInstance))
        {
            Fluxion_SceneInternal_SetError(record, "a component the reload made could not be held onto");
            built = false;
            break;
        }

        const FluxionGameObjectHandle owner = record->components[entry.record].owner;
        ScriptValue arguments[2];
        arguments[0].type = ValueType::Handle;
        arguments[0].handleValue = ToEngineHandle(owner);
        arguments[1].type = ValueType::Handle;
        arguments[1].handleValue = ToEngineHandle(owner);

        auto bound = InvokeMethod(newVm, entry.newInstance, newBindMethod, arguments, 2);
        if (!bound.IsOk())
        {
            ReportFault(record, "handing its object to", entry.className.c_str());
            built = false;
            break;
        }

        RestoreComponent(newVm, entry, newComponentClass, outReport);
    }

    if (!built)
    {
        record->vm = oldVmPointer;
        record->componentClass = oldComponentClass;
        record->bindMethod = oldBindMethod;

        // Whatever was counted on the way to giving up describes instances
        // that are about to be thrown away with the machine that holds
        // them, so it describes nothing.
        outReport = ReloadReport{};
        return false;
    }

    // --- The swap --------------------------------------------------------

    for (CarriedComponent& entry : carried)
    {
        FluxionSceneComponentRecord& component = record->components[entry.record];

        // The scene was the only thing holding the old instance up. Let go
        // of it here rather than leaving it to the machine going away, so
        // a collection on that machine reclaims it: what the scene holds
        // and what the scene has stopped holding stays the truth right up
        // to the moment the machine is released.
        UnpinObject(oldVm, InstanceOf(component));

        component.classIndex = entry.newClass;
        component.instanceIndex = entry.newInstance.index;
        component.instanceGeneration = entry.newInstance.generation;
        for (u32 i = 0; i < FLUXION_SCENE_LIFECYCLE_COUNT; ++i) component.methods[i] = entry.newMethods[i];

        // A component that had already been woken and started is not woken
        // and started again: a reload puts new code under something that
        // is running, and running it from the beginning would undo the
        // very state that was just carried across.
        component.awakePending = entry.awakePending;
        component.startPending = entry.startPending;

        ++outReport.componentsCarried;
    }

    guard.Keep();
    outReport.reloaded = true;
    outReport.retired = oldVm;

    // Described afresh: the classes are the new module's now, and a
    // description left over from the old one would name fields that are
    // gone.
    PublishScriptReflection(record);

    Fluxion_SceneInternal_SetError(record, nullptr);

    FLUXION_LOG_INFO(kLogChannel, "Reloaded %u component(s): %u field value(s) carried across, %u left behind.",
        outReport.componentsCarried, outReport.fieldsCarried, outReport.fieldsDropped);
    return true;
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

    u32 methods[FLUXION_SCENE_LIFECYCLE_COUNT];
    std::string problem;
    if (!ResolveLifecycle(vm, classIndex, ClassNameOf(record, classIndex), methods, problem))
    {
        Fluxion_SceneInternal_SetError(record, problem.c_str());
        FLUXION_LOG_ERROR(kLogChannel, "%s", problem.c_str());
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
    u32* head = ScriptListHead(record, owner);
    if (*head == FLUXION_SCENE_NO_COMPONENT)
    {
        *head = slot;
    }
    else
    {
        u32 cursor = *head;
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

    const u32* head = ScriptListHead(record, owner);
    if (head == nullptr) return 0;

    u32 count = 0;
    u32 cursor = *head;
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
    Fluxion::Scene::ReleaseScriptReflection(record);
    record->vm = nullptr;
}

// The script half of a step, split where the phases split it: what a
// script does every step is simulation, and what it does once everything
// else has moved is not.
//
// Both are registered as systems, so they are ordered against anything
// else the scene runs by the same rules everything else is -- rather than
// being a step of their own that other work has to be fitted around.
extern "C" void Fluxion_SceneComponents_RunSimulation(FluxionSceneHandle scene, f32 deltaTime, void* userData)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    (void)userData;
    if (record == nullptr || record->vm == nullptr) return;

    // What the table held when this began. A component attached while it
    // runs lands above the line and is picked up next step, which is what
    // keeps one step from calling a component into being and then straight
    // away calling into it.
    const u32 limit = record->componentHighWater;

    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_AWAKE, limit, deltaTime, true);
    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_START, limit, deltaTime, true);
    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_UPDATE, limit, deltaTime, false);
}

extern "C" void Fluxion_SceneComponents_RunPostSimulation(FluxionSceneHandle scene, f32 deltaTime, void* userData)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    (void)userData;
    if (record == nullptr || record->vm == nullptr) return;

    Fluxion::Scene::DispatchStep(record, FLUXION_SCENE_LIFECYCLE_LATE_UPDATE, record->componentHighWater, deltaTime, false);
}

extern "C" void Fluxion_Scene_Tick(FluxionSceneHandle scene, f32 deltaTime)
{
    // One step is now nothing but running the scene's systems: the script
    // lifecycle and the world-matrix update are two of them, put in the
    // first time a scene is stepped, and anything else a caller registered
    // sits among them in the order its declarations ask for.
    Fluxion_Scene_RunSystems(scene, deltaTime);
}
