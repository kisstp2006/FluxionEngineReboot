// Putting the scripting language's classes into the engine's own type
// registry, so that a script component is described in exactly the place
// everything else is.
//
// What this is not: a second copy of what the machine already knows. The
// machine's field table stays the one source -- these descriptors point
// back into it, and the accessors read and write through the machine's
// own field access. Nothing here caches a value, and nothing here decides
// what a field means.
//
// What it is: a translation of that table into the shape the rest of the
// engine reads types in. Before this, anything wanting to write a script
// component out or show it in an editor had to know the scripting
// runtime's own headers. Now it needs to know the registry, which it
// already had to.
//
// Rebuilt whenever the classes can have changed -- when a machine is
// attached and after every reload -- because a reload is exactly the case
// where a stale description would describe fields that no longer exist.

#include <Fluxion/Scene/ScriptReflection.h>
#include <Fluxion/Scene/SceneScript.hpp>

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>

#include <string>
#include <vector>

using namespace Fluxion::Script;

namespace Fluxion::Scene
{

namespace
{

const char* const kLogChannel = "Scene.ScriptReflection";

// What one property needs to know to find its field again. Held by the
// scene for as long as the type is registered, and handed to the
// accessors as their context.
struct ScriptFieldContext
{
    // Which class declared it. Not the class of the component being read
    // -- a derived class's component reads a base class's field through
    // the base's own descriptor -- so this is the class the FIELD belongs
    // to, and the instance says which object.
    u32 declaringClass = 0;
    u32 fieldIndex = 0;

    // Where a text value read out of the machine is put so that the
    // pointer handed back outlives the read.
    //
    // The machine answers a string field with a copy, and a copy made
    // inside the getter would be gone by the time the caller looked at
    // it. So it is kept here, per property, and the contract is the one
    // the engine already states for a name: good until the next read of
    // this same property.
    std::string text;
};

struct ScriptClassReflection
{
    std::string name;
    std::vector<std::string> propertyNames;
    std::vector<ScriptFieldContext> contexts;
    std::vector<FluxionPropertyInfo> properties;
    FluxionTypeInfo info{};
};

// Everything one scene publishes. Held by the scene because the registry
// keeps pointers rather than copies: the moment this goes, the registry
// is pointing at nothing.
struct SceneScriptReflection
{
    // Deque-like stability is what matters here, not speed: the registry
    // holds a pointer to each `info`, so the entries must not move once
    // they are handed over. Reserved to the class count before anything
    // is added.
    std::vector<ScriptClassReflection> classes;
};

// The engine type id a script field's declared type answers to.
//
// The scalar three are the engine's own; text and object references are
// the two the binding layer already names, so a script field and a script
// method parameter of the same kind carry the same id. A field of a value
// type or an enum is named by that type's own name, which is what makes
// two classes agreeing on a value type agree on its id too.
FluxionTypeId TypeIdOfField(Vm* vm, const FieldInfo& field)
{
    switch (field.type)
    {
        case ValueType::Bool:   return FLUXION_TYPE_ID_OF(bool);
        case ValueType::Int:    return FLUXION_TYPE_ID_OF(i32);
        case ValueType::Float:  return FLUXION_TYPE_ID_OF(f32);
        case ValueType::String: return ScriptStringTypeId();
        case ValueType::Object: return ScriptObjectTypeId();

        case ValueType::Struct:
        case ValueType::Enum:
        {
            const char* name = (field.typeClass != kNoClass) ? ClassName(vm, field.typeClass) : nullptr;
            return (name != nullptr) ? Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(name)) : FLUXION_TYPE_ID_INVALID;
        }

        // A handle names something of the engine's, and what it names is
        // the engine's business -- so it is described as an engine handle
        // and nothing finer. Resolving one belongs to whoever knows what
        // the handle is for.
        case ValueType::Handle: return FLUXION_TYPE_ID_OF(EngineHandle);

        default: return FLUXION_TYPE_ID_INVALID;
    }
}

// How wide the value a caller must provide storage for is.
usize ValueSizeOfField(const FieldInfo& field)
{
    switch (field.type)
    {
        case ValueType::Bool:   return sizeof(bool);
        case ValueType::Int:    return sizeof(i32);
        case ValueType::Float:  return sizeof(f32);
        case ValueType::String: return sizeof(FluxionScriptString);
        case ValueType::Object: return sizeof(FluxionScriptObject);
        case ValueType::Handle: return sizeof(EngineHandle);
        default:                return 0;
    }
}

// Resolves a property read or write down to the one thing that can carry
// it out: this machine, this object, this field.
bool ResolveField(const FluxionScriptInstance& instance, Vm*& outVm, ObjectHandle& outObject, const FieldInfo*& outField,
                  const ScriptFieldContext& context)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(instance.scene);
    if (record == nullptr || record->vm == nullptr) return false;

    outVm = (Vm*)record->vm;
    outField = ClassFieldAt(outVm, context.declaringClass, context.fieldIndex);
    if (outField == nullptr) return false;

    {
        const u32 componentIndex = FindComponentIndex(record, instance.object, instance.classIndex);
        if (componentIndex == FLUXION_SCENE_NO_COMPONENT) return false;

        const FluxionSceneComponentRecord& component = record->components[componentIndex];
        outObject.index = component.instanceIndex;
        outObject.generation = component.instanceGeneration;
    }
    return true;
}

// The one getter every script property shares. Which field it is about
// comes from the context; which object, from the instance.
void ScriptPropertyGet(const void* instancePointer, void* outValue, void* contextPointer)
{
    const auto& instance = *(const FluxionScriptInstance*)instancePointer;
    auto& context = *(ScriptFieldContext*)contextPointer;

    Vm* vm = nullptr;
    ObjectHandle object;
    const FieldInfo* field = nullptr;
    ScriptValue value;

    if (!ResolveField(instance, vm, object, field, context)) return;
    if (!ReadInstanceField(vm, object, *field, value)) return;

    switch (field->type)
    {
        case ValueType::Bool:   *(bool*)outValue = value.boolValue; break;
        case ValueType::Int:    *(i32*)outValue = value.intValue; break;
        case ValueType::Float:  *(f32*)outValue = value.floatValue; break;

        // Kept in the property rather than answered from a local, which
        // would be gone before the caller read it. Good until the next
        // read of this same property; a caller keeping the text copies
        // it, which is the same rule every string leaving the machine
        // already follows.
        case ValueType::String:
            context.text = value.stringValue;
            *(FluxionScriptString*)outValue = context.text.c_str();
            break;

        case ValueType::Object: *(FluxionScriptObject*)outValue = value.objectValue; break;
        case ValueType::Handle: *(EngineHandle*)outValue = value.handleValue; break;
        default: break;
    }
}

void ScriptPropertySet(void* instancePointer, const void* valuePointer, void* contextPointer)
{
    const auto& instance = *(const FluxionScriptInstance*)instancePointer;
    const auto& context = *(const ScriptFieldContext*)contextPointer;

    Vm* vm = nullptr;
    ObjectHandle object;
    const FieldInfo* field = nullptr;

    if (!ResolveField(instance, vm, object, field, context)) return;

    ScriptValue value;
    value.type = field->type;
    switch (field->type)
    {
        case ValueType::Bool:   value.boolValue = *(const bool*)valuePointer; break;
        case ValueType::Int:    value.intValue = *(const i32*)valuePointer; break;
        case ValueType::Float:  value.floatValue = *(const f32*)valuePointer; break;
        case ValueType::String:
        {
            const FluxionScriptString text = *(const FluxionScriptString*)valuePointer;
            value.stringValue = (text != nullptr) ? text : "";
            break;
        }
        case ValueType::Object: value.objectValue = *(const FluxionScriptObject*)valuePointer; break;
        case ValueType::Handle: value.handleValue = *(const EngineHandle*)valuePointer; break;
        default: return;
    }

    (void)WriteInstanceField(vm, object, *field, value);
}

// Describes one class: its own fields and those of every class it was
// built on, stopping short of the one every component is built on -- the
// same boundary the reload already draws, and for the same reason. Those
// two fields are the object and its transform, handed over afresh
// whenever an instance is made, so describing them would invite something
// to write them.
void DescribeClass(Vm* vm, u32 classIndex, u32 stopAt, ScriptClassReflection& out)
{
    const char* className = ClassName(vm, classIndex);
    out.name = (className != nullptr) ? className : "";

    u32 current = classIndex;
    while (current != kNoClass && current != stopAt)
    {
        const u32 count = ClassFieldCount(vm, current);
        for (u32 i = 0; i < count; ++i)
        {
            const FieldInfo* field = ClassFieldAt(vm, current, i);
            if (field == nullptr) continue;

            const usize size = ValueSizeOfField(*field);
            if (size == 0) continue; // a value type or an enum: no flat value to carry yet

            out.propertyNames.push_back(field->name);
            out.contexts.push_back(ScriptFieldContext{ current, i, std::string() });
        }
        current = ClassBaseClass(vm, current);
    }

    // Built only once both vectors have stopped growing: they hold the
    // storage the descriptors point at, and a vector that grows moves it.
    out.properties.reserve(out.propertyNames.size());
    for (usize i = 0; i < out.propertyNames.size(); ++i)
    {
        const FieldInfo* field = ClassFieldAt(vm, out.contexts[i].declaringClass, out.contexts[i].fieldIndex);

        FluxionPropertyInfo property{};
        property.name = Fluxion_StringView_FromCStr(out.propertyNames[i].c_str());
        property.type = TypeIdOfField(vm, *field);
        property.size = ValueSizeOfField(*field);
        property.flags = FLUXION_PROPERTY_FLAG_NONE;
        property.accessKind = FLUXION_PROPERTY_ACCESS_ACCESSOR;
        property.accessor.getter = &ScriptPropertyGet;
        property.accessor.setter = &ScriptPropertySet;
        property.accessor.context = &out.contexts[i];
        out.properties.push_back(property);
    }

    out.info.name = Fluxion_StringView_FromCStr(out.name.c_str());
    out.info.id = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(out.name.c_str()));
    out.info.kind = FLUXION_TYPE_KIND_STRUCT;

    // Zero on purpose: a script class is not bytes an entity can own, and
    // a size of zero is what the storage refuses to lay a column out for.
    out.info.size = 0;

    out.info.version = 1;
    out.info.members = Fluxion_Span_Make(out.properties.data(), out.properties.size(), sizeof(FluxionPropertyInfo));
    out.info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
}

} // namespace

void ReleaseScriptReflection(FluxionSceneRecord* record)
{
    if (record == nullptr || record->scriptReflection == nullptr) return;

    auto* published = (SceneScriptReflection*)record->scriptReflection;

    // Taken out of the registry BEFORE the storage goes. The registry
    // holds the pointer rather than a copy, so the other order would
    // leave it naming memory that is no longer there -- and the thing
    // that followed it would be whatever walked the registry next, which
    // is nowhere near here.
    if (Fluxion_Reflection_IsInitialized())
    {
        for (const ScriptClassReflection& described : published->classes)
        {
            (void)Fluxion_Reflection_UnregisterType(described.info.id);
        }
    }

    delete published;
    record->scriptReflection = nullptr;
}

void PublishScriptReflection(FluxionSceneRecord* record)
{
    ReleaseScriptReflection(record);

    if (record == nullptr || record->vm == nullptr) return;
    if (!Fluxion_Reflection_IsInitialized()) return;

    Vm* vm = (Vm*)record->vm;
    const u32 componentClass = record->componentClass;
    const u32 classCount = ClassCount(vm);

    auto* published = new (std::nothrow) SceneScriptReflection();
    if (published == nullptr) return;

    // Reserved before anything is added: the registry is handed a pointer
    // to each entry, and a vector that grows would move them out from
    // under it.
    published->classes.reserve(classCount);

    for (u32 classIndex = 0; classIndex < classCount; ++classIndex)
    {
        if (!ClassDerivesFrom(vm, classIndex, componentClass)) continue;
        if (classIndex == componentClass) continue;

        published->classes.emplace_back();
        DescribeClass(vm, classIndex, componentClass, published->classes.back());

        if (!Fluxion_Reflection_RegisterType(&published->classes.back().info))
        {
            FLUXION_LOG_WARN(kLogChannel, "'%s' could not be published to the type registry",
                published->classes.back().name.c_str());
        }
    }

    record->scriptReflection = published;
}

} // namespace Fluxion::Scene

// --- What a caller outside this module sees ------------------------------

extern "C" FluxionTypeId Fluxion_Scene_ScriptClassTypeId(FluxionSceneHandle scene, u32 classIndex)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == nullptr || record->vm == nullptr) return FLUXION_TYPE_ID_INVALID;

    {
        const char* name = ClassName((Vm*)record->vm, classIndex);
        if (name == nullptr) return FLUXION_TYPE_ID_INVALID;
        return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(name));
    }
}

extern "C" u32 Fluxion_GameObject_GetScriptComponents(FluxionSceneHandle scene, FluxionGameObjectHandle object,
                                                     FluxionScriptComponentRef* out, u32 max)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    u32 found = 0;

    if (entry == nullptr) return 0;

    {
        const u32* head = Fluxion::Scene::ScriptListHead(record, entry);
        if (head == nullptr) return 0;

        for (u32 cursor = *head; cursor != FLUXION_SCENE_NO_COMPONENT; cursor = record->components[cursor].nextOnOwner)
        {
            const FluxionSceneComponentRecord& component = record->components[cursor];
            if (!component.inUse || component.removing) continue;

            if (found < max && out != nullptr)
            {
                out[found].type = Fluxion_Scene_ScriptClassTypeId(scene, component.classIndex);
                out[found].instance.scene = scene;
                out[found].instance.object = object;
                out[found].instance.classIndex = component.classIndex;
            }
            ++found;
        }
    }
    return found;
}
