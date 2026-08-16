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

#include <Fluxion/Scene/EngineScript.hpp>

#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Time/Time.h>
#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RenderCore/Renderer/DebugDraw.h>

#include <cstring>
#include <string>
#include <vector>

using namespace Fluxion::Script;

namespace Fluxion::Scene
{

namespace
{

// --- What the host has put within reach ---------------------------------

constexpr u32 kMaxNamedAssets = 32;
constexpr u32 kMaxAssetNameLength = 63;

// A handful of names for each kind of thing, held in place rather than
// grown: what goes in here is what one program made once at startup, and
// a fixed table means a name can be looked up while a frame is being
// drawn without anything being allocated to do it.
template<typename HandleT>
struct NamedHandles
{
    struct Entry
    {
        char name[kMaxAssetNameLength + 1];
        HandleT handle;
        bool used;
    };

    Entry entries[kMaxNamedAssets] = {};

    static bool NameFits(const char* name) { return name != nullptr && name[0] != '\0' && std::strlen(name) <= kMaxAssetNameLength; }

    bool Put(const char* name, HandleT handle)
    {
        if (!NameFits(name)) return false;

        Entry* free = nullptr;
        for (Entry& entry : entries)
        {
            if (entry.used && std::strcmp(entry.name, name) == 0)
            {
                entry.handle = handle;
                return true;
            }
            if (!entry.used && free == nullptr) free = &entry;
        }
        if (free == nullptr) return false;

        // The length was established by NameFits above, so the terminator
        // is copied along with the text rather than written afterwards.
        std::memcpy(free->name, name, std::strlen(name) + 1);
        free->handle = handle;
        free->used = true;
        return true;
    }

    const HandleT* Find(const char* name) const
    {
        if (!NameFits(name)) return nullptr;
        for (const Entry& entry : entries)
        {
            if (entry.used && std::strcmp(entry.name, name) == 0) return &entry.handle;
        }
        return nullptr;
    }

    void Clear()
    {
        for (Entry& entry : entries) entry.used = false;
    }
};

NamedHandles<FluxionMeshBufferHandle> s_meshes;
NamedHandles<FluxionMaterialHandle> s_materials;
NamedHandles<FluxionRenderPipelineHandle> s_pipelines;

FluxionRendererHandle s_renderer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

// Which scene the object handed to Renderer.DrawMesh is looked up in.
// Written when the table is built, because a table is built for one scene
// and the handles a script passes through it can only mean that scene's
// objects.
FluxionSceneHandle s_scene = { FLUXION_HANDLE_INVALID_INDEX, 0 };

// --- Turning handles back and forth --------------------------------------

EngineHandle NoHandle()
{
    EngineHandle handle;
    handle.index = FLUXION_HANDLE_INVALID_INDEX;
    handle.generation = 0;
    return handle;
}

template<typename HandleT>
EngineHandle ToScript(HandleT handle)
{
    EngineHandle result;
    result.index = handle.index;
    result.generation = handle.generation;
    return result;
}

template<typename HandleT>
HandleT FromScript(EngineHandle handle)
{
    HandleT result;
    result.index = handle.index;
    result.generation = handle.generation;
    return result;
}

// --- Time ----------------------------------------------------------------

// Every one of these answers the same thing for the whole of a frame:
// what the clock says is settled once, by whoever begins the frame, and
// nothing here advances it.
struct ScriptTime
{
    static f32 DeltaTime() { return Fluxion_Time_GetDeltaTime(); }
    static f32 UnscaledDeltaTime() { return Fluxion_Time_GetUnscaledDeltaTime(); }

    // Narrowed from the double the clock keeps: a script does arithmetic
    // in floats and has nothing wider to put this in. After a few hours
    // of running, consecutive frames stop being distinguishable here --
    // anything that has to keep counting for longer than that should be
    // adding up deltas of its own rather than reading this.
    static f32 ElapsedTime() { return (f32)Fluxion_Time_GetElapsedTime(); }
    static f32 UnscaledElapsedTime() { return (f32)Fluxion_Time_GetUnscaledElapsedTime(); }

    // Likewise narrowed. At sixty frames a second this stops being right
    // after a bit over a year of continuous running.
    static i32 FrameCount() { return (i32)Fluxion_Time_GetFrameCount(); }

    static f32 TimeScale() { return Fluxion_Time_GetTimeScale(); }
    static void SetTimeScale(f32 scale) { Fluxion_Time_SetTimeScale(scale); }

    static f32 MaximumDeltaTime() { return Fluxion_Time_GetMaximumDeltaTime(); }
    static void SetMaximumDeltaTime(f32 seconds) { Fluxion_Time_SetMaximumDeltaTime(seconds); }
};

// --- Input ---------------------------------------------------------------

// A script names a key with a constant of a set, and the constant travels
// as the number it stands for -- so what arrives here is a number, and
// the range check is what keeps a number that named nothing from reaching
// into the state arrays at all.
struct ScriptInput
{
    static bool NamesAKey(i32 key) { return key >= 0 && key < FLUXION_KEY_COUNT; }
    static bool NamesAMouseButton(i32 button) { return button >= 0 && button < FLUXION_MOUSE_BUTTON_COUNT; }
    static bool NamesAGamepadButton(i32 button) { return button >= 0 && button < FLUXION_GAMEPAD_BUTTON_COUNT; }
    static bool NamesAGamepadAxis(i32 axis) { return axis >= 0 && axis < FLUXION_GAMEPAD_AXIS_COUNT; }

    static bool IsKeyDown(i32 key) { return NamesAKey(key) && Fluxion_Input_IsKeyDown((FluxionKeyCode)key); }
    static bool WasKeyPressed(i32 key) { return NamesAKey(key) && Fluxion_Input_WasKeyPressed((FluxionKeyCode)key); }
    static bool WasKeyReleased(i32 key) { return NamesAKey(key) && Fluxion_Input_WasKeyReleased((FluxionKeyCode)key); }

    static bool IsMouseButtonDown(i32 button)
    {
        return NamesAMouseButton(button) && Fluxion_Input_IsMouseButtonDown((FluxionMouseButton)button);
    }
    static bool WasMouseButtonPressed(i32 button)
    {
        return NamesAMouseButton(button) && Fluxion_Input_WasMouseButtonPressed((FluxionMouseButton)button);
    }
    static bool WasMouseButtonReleased(i32 button)
    {
        return NamesAMouseButton(button) && Fluxion_Input_WasMouseButtonReleased((FluxionMouseButton)button);
    }

    // The pointer's place and how far it moved cross as one number each,
    // because a pair of them is not something a native can be handed.
    static i32 MouseX()
    {
        i32 x = 0, y = 0;
        Fluxion_Input_GetMousePosition(&x, &y);
        return x;
    }
    static i32 MouseY()
    {
        i32 x = 0, y = 0;
        Fluxion_Input_GetMousePosition(&x, &y);
        return y;
    }
    static i32 MouseDeltaX()
    {
        i32 x = 0, y = 0;
        Fluxion_Input_GetMouseDelta(&x, &y);
        return x;
    }
    static i32 MouseDeltaY()
    {
        i32 x = 0, y = 0;
        Fluxion_Input_GetMouseDelta(&x, &y);
        return y;
    }
    static f32 MouseScroll() { return Fluxion_Input_GetMouseScrollDelta(); }

    static bool IsGamepadConnected(i32 gamepad)
    {
        FluxionGamepadState state;
        if (gamepad < 0 || !Fluxion_Input_GetGamepadState((u32)gamepad, &state)) return false;
        return state.connected;
    }

    // There is no "pressed this frame" for a gamepad, here or anywhere
    // else: gamepads are polled rather than delivered as events, so the
    // input system underneath keeps only what is held down now. A script
    // that wants the edge remembers last frame's answer itself.
    static bool IsGamepadButtonDown(i32 gamepad, i32 button)
    {
        FluxionGamepadState state;
        if (gamepad < 0 || !NamesAGamepadButton(button)) return false;
        if (!Fluxion_Input_GetGamepadState((u32)gamepad, &state)) return false;
        return state.connected && state.buttons[button];
    }

    static f32 GamepadAxis(i32 gamepad, i32 axis)
    {
        FluxionGamepadState state;
        if (gamepad < 0 || !NamesAGamepadAxis(axis)) return 0.0f;
        if (!Fluxion_Input_GetGamepadState((u32)gamepad, &state)) return 0.0f;
        return state.connected ? state.axes[axis] : 0.0f;
    }
};

// --- Material ------------------------------------------------------------

// What a handle to one of the host's materials turns into for the length
// of one call. Unlike a scene's objects, one of these is enough: a call
// into the engine resolves its receiver and then makes the call with
// nothing in between, so no second resolution can happen while this one
// is still being used.
struct ScriptMaterial
{
    FluxionMaterialHandle self;

    // Each answers whether the material's shader actually declares a
    // parameter of that name and shape. False is not a failure to be
    // reported: asking a material for something it does not have is a
    // reasonable question with a plain answer.
    bool SetFloat(FluxionScriptString name, f32 value) { return Fluxion_Material_SetFloat(self, name, value); }
    bool SetVec3(FluxionScriptString name, f32 x, f32 y, f32 z)
    {
        return Fluxion_Material_SetVec3(self, name, FluxionVec3{ x, y, z });
    }
    bool SetVec4(FluxionScriptString name, f32 x, f32 y, f32 z, f32 w)
    {
        return Fluxion_Material_SetVec4(self, name, FluxionVec4{ x, y, z, w });
    }

    // Nothing set above reaches a draw until this runs, so a script that
    // changes a parameter and then draws has to do this in between.
    void FlushDirty() { Fluxion_Material_FlushDirty(self); }
};

ScriptMaterial s_materialView;

void* ResolveMaterial(void* user, EngineHandle handle)
{
    (void)user;

    // Only the handle that names nothing is refused here. Whether the
    // material behind a real handle still exists is not something this
    // module can ask, and it does not have to: every call above answers
    // false, or does nothing, for a handle the renderer no longer knows.
    if (handle.index == FLUXION_HANDLE_INVALID_INDEX) return nullptr;

    s_materialView.self = FromScript<FluxionMaterialHandle>(handle);
    return &s_materialView;
}

// --- Naming what the host made -------------------------------------------

struct ScriptAssets
{
    static bool HasMesh(FluxionScriptString name) { return s_meshes.Find(name) != nullptr; }
    static bool HasMaterial(FluxionScriptString name) { return s_materials.Find(name) != nullptr; }
    static bool HasPipeline(FluxionScriptString name) { return s_pipelines.Find(name) != nullptr; }

    // A name nothing was registered under answers with a handle that
    // names nothing, the same way asking an object for a parent it has
    // not got does. Drawing with one draws nothing; reaching through one
    // for a material's parameters stops the script.
    static EngineHandle FindMesh(FluxionScriptString name)
    {
        const FluxionMeshBufferHandle* found = s_meshes.Find(name);
        return found ? ToScript(*found) : NoHandle();
    }
    static EngineHandle FindMaterial(FluxionScriptString name)
    {
        const FluxionMaterialHandle* found = s_materials.Find(name);
        return found ? ToScript(*found) : NoHandle();
    }
    static EngineHandle FindPipeline(FluxionScriptString name)
    {
        const FluxionRenderPipelineHandle* found = s_pipelines.Find(name);
        return found ? ToScript(*found) : NoHandle();
    }
};

// --- Drawing --------------------------------------------------------------

// The renderer is handed matrices in the byte order the shaders read them
// in, and does no rearranging of its own on the way to the buffer -- so
// the world matrix an object carries, which is written the way the
// arithmetic reads, is turned around here, at the one place a script's
// draw crosses over.
FluxionMat4 ForUpload(FluxionMat4 matrix)
{
    FluxionMat4 turned;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            turned.m[row][column] = matrix.m[column][row];
    return turned;
}

struct ScriptRenderer
{
    // Where the mesh goes is the object's business, not the caller's:
    // a matrix is not something that can be handed to a native, and a
    // script that could pass one would only be passing on what the
    // object it already named knows.
    static void DrawMesh(EngineHandle mesh, EngineHandle material, EngineHandle pipeline, EngineHandle object)
    {
        if (!FLUXION_HANDLE_IS_VALID(s_renderer)) return;

        const FluxionMat4 world = ForUpload(Fluxion_GameObject_GetWorldMatrix(s_scene, FromScript<FluxionGameObjectHandle>(object)));
        Fluxion_Renderer_DrawMesh(s_renderer, FromScript<FluxionMeshBufferHandle>(mesh), FromScript<FluxionMaterialHandle>(material),
            FromScript<FluxionRenderPipelineHandle>(pipeline), &world);
    }
};

// A position and a colour cross as the numbers they are made of. What a
// script writes instead is the shaped form in the prelude, which is these
// same calls with the numbers taken out of a Vector3 and a Color.
struct ScriptDebugDraw
{
    static void Line(f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz, f32 r, f32 g, f32 b, f32 a)
    {
        Fluxion_DebugDraw_Line(s_renderer, FluxionVec3{ ax, ay, az }, FluxionVec3{ bx, by, bz }, FluxionVec4{ r, g, b, a });
    }

    static void Triangle(f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz, f32 cx, f32 cy, f32 cz, f32 r, f32 g, f32 b, f32 a)
    {
        Fluxion_DebugDraw_Triangle(s_renderer, FluxionVec3{ ax, ay, az }, FluxionVec3{ bx, by, bz }, FluxionVec3{ cx, cy, cz },
            FluxionVec4{ r, g, b, a });
    }
};

// --- Describing all of it to the binding table ---------------------------

// Owns every array the descriptors point at for as long as the table is
// being built, which is all the binding table needs: it copies what it
// keeps.
class EngineDescriptors
{
public:
    EngineDescriptors()
    {
        const FluxionTypeId intType = FLUXION_TYPE_ID_OF(i32);
        const FluxionTypeId floatType = FLUXION_TYPE_ID_OF(f32);
        const FluxionTypeId boolType = FLUXION_TYPE_ID_OF(bool);
        const FluxionTypeId stringType = ScriptStringTypeId();
        const FluxionTypeId voidType = FLUXION_TYPE_ID_INVALID;

        m_meshTypeId = FLUXION_TYPE_ID_OF(FluxionScriptMesh);
        m_materialTypeId = FLUXION_TYPE_ID_OF(FluxionScriptMaterial);
        m_pipelineTypeId = FLUXION_TYPE_ID_OF(FluxionScriptRenderPipeline);

        m_oneInt[0] = intType;
        m_twoInts[0] = intType;
        m_twoInts[1] = intType;
        m_oneFloat[0] = floatType;
        m_oneString[0] = stringType;

        m_stringFloat[0] = stringType;
        m_stringFloat[1] = floatType;

        m_stringThreeFloats[0] = stringType;
        for (u32 i = 1; i < 4; ++i) m_stringThreeFloats[i] = floatType;

        m_stringFourFloats[0] = stringType;
        for (u32 i = 1; i < 5; ++i) m_stringFourFloats[i] = floatType;

        for (FluxionTypeId& id : m_tenFloats) id = floatType;
        for (FluxionTypeId& id : m_thirteenFloats) id = floatType;

        m_drawMesh[0] = m_meshTypeId;
        m_drawMesh[1] = m_materialTypeId;
        m_drawMesh[2] = m_pipelineTypeId;
        m_drawMesh[3] = FLUXION_TYPE_ID_OF(FluxionGameObject);

        // --- The clock ---------------------------------------------------

        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::DeltaTime>("DeltaTime", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::UnscaledDeltaTime>("UnscaledDeltaTime", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::ElapsedTime>("ElapsedTime", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::UnscaledElapsedTime>("UnscaledElapsedTime", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::FrameCount>("FrameCount", intType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::TimeScale>("TimeScale", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::SetTimeScale>("SetTimeScale", voidType, m_oneFloat));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::MaximumDeltaTime>("MaximumDeltaTime", floatType));
        m_timeMethods.push_back(Core::ReflectMethod<&ScriptTime::SetMaximumDeltaTime>("SetMaximumDeltaTime", voidType, m_oneFloat));

        // --- What is being held down -------------------------------------

        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::IsKeyDown>("IsKeyDown", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::WasKeyPressed>("WasKeyPressed", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::WasKeyReleased>("WasKeyReleased", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::IsMouseButtonDown>("IsMouseButtonDown", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::WasMouseButtonPressed>("WasMouseButtonPressed", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::WasMouseButtonReleased>("WasMouseButtonReleased", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::MouseX>("MouseX", intType));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::MouseY>("MouseY", intType));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::MouseDeltaX>("MouseDeltaX", intType));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::MouseDeltaY>("MouseDeltaY", intType));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::MouseScroll>("MouseScroll", floatType));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::IsGamepadConnected>("IsGamepadConnected", boolType, m_oneInt));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::IsGamepadButtonDown>("IsGamepadButtonDown", boolType, m_twoInts));
        m_inputMethods.push_back(Core::ReflectMethod<&ScriptInput::GamepadAxis>("GamepadAxis", floatType, m_twoInts));

        // --- One of the host's materials ----------------------------------

        m_materialMethods.push_back(Core::ReflectMethod<&ScriptMaterial::SetFloat>("SetFloat", boolType, m_stringFloat));
        m_materialMethods.push_back(Core::ReflectMethod<&ScriptMaterial::SetVec3>("SetVec3", boolType, m_stringThreeFloats));
        m_materialMethods.push_back(Core::ReflectMethod<&ScriptMaterial::SetVec4>("SetVec4", boolType, m_stringFourFloats));
        m_materialMethods.push_back(Core::ReflectMethod<&ScriptMaterial::FlushDirty>("FlushDirty", voidType));

        // --- Naming things ------------------------------------------------

        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::HasMesh>("HasMesh", boolType, m_oneString));
        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::HasMaterial>("HasMaterial", boolType, m_oneString));
        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::HasPipeline>("HasPipeline", boolType, m_oneString));
        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::FindMesh>("FindMesh", m_meshTypeId, m_oneString));
        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::FindMaterial>("FindMaterial", m_materialTypeId, m_oneString));
        m_assetMethods.push_back(Core::ReflectMethod<&ScriptAssets::FindPipeline>("FindPipeline", m_pipelineTypeId, m_oneString));

        // --- Putting something on the screen ------------------------------

        m_rendererMethods.push_back(Core::ReflectMethod<&ScriptRenderer::DrawMesh>("DrawMesh", voidType, m_drawMesh));
        m_debugDrawMethods.push_back(Core::ReflectMethod<&ScriptDebugDraw::Line>("Line", voidType, m_tenFloats));
        m_debugDrawMethods.push_back(Core::ReflectMethod<&ScriptDebugDraw::Triangle>("Triangle", voidType, m_thirteenFloats));

        Describe(m_meshType, "Mesh", m_meshTypeId, sizeof(FluxionMeshBufferHandle), m_meshMethods);
        Describe(m_materialType, "Material", m_materialTypeId, sizeof(ScriptMaterial), m_materialMethods);
        Describe(m_pipelineType, "RenderPipeline", m_pipelineTypeId, sizeof(FluxionRenderPipelineHandle), m_pipelineMethods);
        Describe(m_timeType, "Time", FLUXION_TYPE_ID_OF(FluxionScriptTime), sizeof(ScriptTime), m_timeMethods);
        Describe(m_inputType, "Input", FLUXION_TYPE_ID_OF(FluxionScriptInput), sizeof(ScriptInput), m_inputMethods);
        Describe(m_assetsType, "Assets", FLUXION_TYPE_ID_OF(FluxionScriptAssets), sizeof(ScriptAssets), m_assetMethods);
        Describe(m_rendererType, "Renderer", FLUXION_TYPE_ID_OF(FluxionScriptRenderer), sizeof(ScriptRenderer), m_rendererMethods);
        Describe(m_debugDrawType, "DebugDraw", FLUXION_TYPE_ID_OF(FluxionScriptDebugDraw), sizeof(ScriptDebugDraw), m_debugDrawMethods);
    }

    EngineDescriptors(const EngineDescriptors&) = delete;
    EngineDescriptors& operator=(const EngineDescriptors&) = delete;

    const FluxionTypeInfo& MeshType() const { return m_meshType; }
    const FluxionTypeInfo& MaterialType() const { return m_materialType; }
    const FluxionTypeInfo& PipelineType() const { return m_pipelineType; }
    const FluxionTypeInfo& TimeType() const { return m_timeType; }
    const FluxionTypeInfo& InputType() const { return m_inputType; }
    const FluxionTypeInfo& AssetsType() const { return m_assetsType; }
    const FluxionTypeInfo& RendererType() const { return m_rendererType; }
    const FluxionTypeInfo& DebugDrawType() const { return m_debugDrawType; }

private:
    static void Describe(FluxionTypeInfo& info, const char* name, FluxionTypeId id, usize size, std::vector<FluxionMethodInfo>& methods)
    {
        info.name = Fluxion_StringView_FromCStr(name);
        info.id = id;
        info.kind = FLUXION_TYPE_KIND_STRUCT;
        info.size = size;
        info.version = 1;
        info.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        info.methods = Fluxion_Span_Make(methods.data(), methods.size(), sizeof(FluxionMethodInfo));
    }

    FluxionTypeId m_meshTypeId{};
    FluxionTypeId m_materialTypeId{};
    FluxionTypeId m_pipelineTypeId{};

    FluxionTypeId m_oneInt[1]{};
    FluxionTypeId m_twoInts[2]{};
    FluxionTypeId m_oneFloat[1]{};
    FluxionTypeId m_oneString[1]{};
    FluxionTypeId m_stringFloat[2]{};
    FluxionTypeId m_stringThreeFloats[4]{};
    FluxionTypeId m_stringFourFloats[5]{};
    FluxionTypeId m_tenFloats[10]{};
    FluxionTypeId m_thirteenFloats[13]{};
    FluxionTypeId m_drawMesh[4]{};

    // Geometry and a pipeline are named and handed back, and nothing
    // else: what either is for is settled entirely by whoever made it.
    std::vector<FluxionMethodInfo> m_meshMethods;
    std::vector<FluxionMethodInfo> m_pipelineMethods;

    std::vector<FluxionMethodInfo> m_materialMethods;
    std::vector<FluxionMethodInfo> m_timeMethods;
    std::vector<FluxionMethodInfo> m_inputMethods;
    std::vector<FluxionMethodInfo> m_assetMethods;
    std::vector<FluxionMethodInfo> m_rendererMethods;
    std::vector<FluxionMethodInfo> m_debugDrawMethods;

    FluxionTypeInfo m_meshType{};
    FluxionTypeInfo m_materialType{};
    FluxionTypeInfo m_pipelineType{};
    FluxionTypeInfo m_timeType{};
    FluxionTypeInfo m_inputType{};
    FluxionTypeInfo m_assetsType{};
    FluxionTypeInfo m_rendererType{};
    FluxionTypeInfo m_debugDrawType{};
};

// --- The sets of constants, and the numbers behind them ------------------

// Each name is written once, against the identity the engine itself
// declares -- so the set a script sees is built from that identity rather
// than from a copy of it, and the two cannot drift apart.
#define FLUXION_SCRIPT_KEYS(EACH) \
    EACH(Unknown, FLUXION_KEY_UNKNOWN) \
    EACH(A, FLUXION_KEY_A) EACH(B, FLUXION_KEY_B) EACH(C, FLUXION_KEY_C) EACH(D, FLUXION_KEY_D) EACH(E, FLUXION_KEY_E) \
    EACH(F, FLUXION_KEY_F) EACH(G, FLUXION_KEY_G) EACH(H, FLUXION_KEY_H) EACH(I, FLUXION_KEY_I) EACH(J, FLUXION_KEY_J) \
    EACH(K, FLUXION_KEY_K) EACH(L, FLUXION_KEY_L) EACH(M, FLUXION_KEY_M) EACH(N, FLUXION_KEY_N) EACH(O, FLUXION_KEY_O) \
    EACH(P, FLUXION_KEY_P) EACH(Q, FLUXION_KEY_Q) EACH(R, FLUXION_KEY_R) EACH(S, FLUXION_KEY_S) EACH(T, FLUXION_KEY_T) \
    EACH(U, FLUXION_KEY_U) EACH(V, FLUXION_KEY_V) EACH(W, FLUXION_KEY_W) EACH(X, FLUXION_KEY_X) EACH(Y, FLUXION_KEY_Y) \
    EACH(Z, FLUXION_KEY_Z) \
    EACH(Digit0, FLUXION_KEY_0) EACH(Digit1, FLUXION_KEY_1) EACH(Digit2, FLUXION_KEY_2) EACH(Digit3, FLUXION_KEY_3) \
    EACH(Digit4, FLUXION_KEY_4) EACH(Digit5, FLUXION_KEY_5) EACH(Digit6, FLUXION_KEY_6) EACH(Digit7, FLUXION_KEY_7) \
    EACH(Digit8, FLUXION_KEY_8) EACH(Digit9, FLUXION_KEY_9) \
    EACH(F1, FLUXION_KEY_F1) EACH(F2, FLUXION_KEY_F2) EACH(F3, FLUXION_KEY_F3) EACH(F4, FLUXION_KEY_F4) \
    EACH(F5, FLUXION_KEY_F5) EACH(F6, FLUXION_KEY_F6) EACH(F7, FLUXION_KEY_F7) EACH(F8, FLUXION_KEY_F8) \
    EACH(F9, FLUXION_KEY_F9) EACH(F10, FLUXION_KEY_F10) EACH(F11, FLUXION_KEY_F11) EACH(F12, FLUXION_KEY_F12) \
    EACH(Escape, FLUXION_KEY_ESCAPE) EACH(Tab, FLUXION_KEY_TAB) EACH(CapsLock, FLUXION_KEY_CAPS_LOCK) \
    EACH(LeftShift, FLUXION_KEY_LEFT_SHIFT) EACH(RightShift, FLUXION_KEY_RIGHT_SHIFT) \
    EACH(LeftControl, FLUXION_KEY_LEFT_CONTROL) EACH(RightControl, FLUXION_KEY_RIGHT_CONTROL) \
    EACH(LeftAlt, FLUXION_KEY_LEFT_ALT) EACH(RightAlt, FLUXION_KEY_RIGHT_ALT) \
    EACH(Space, FLUXION_KEY_SPACE) EACH(Enter, FLUXION_KEY_ENTER) EACH(Backspace, FLUXION_KEY_BACKSPACE) \
    EACH(Delete, FLUXION_KEY_DELETE) EACH(Insert, FLUXION_KEY_INSERT) EACH(Home, FLUXION_KEY_HOME) \
    EACH(End, FLUXION_KEY_END) EACH(PageUp, FLUXION_KEY_PAGE_UP) EACH(PageDown, FLUXION_KEY_PAGE_DOWN) \
    EACH(Up, FLUXION_KEY_UP) EACH(Down, FLUXION_KEY_DOWN) EACH(Left, FLUXION_KEY_LEFT) EACH(Right, FLUXION_KEY_RIGHT) \
    EACH(Numpad0, FLUXION_KEY_NUMPAD_0) EACH(Numpad1, FLUXION_KEY_NUMPAD_1) EACH(Numpad2, FLUXION_KEY_NUMPAD_2) \
    EACH(Numpad3, FLUXION_KEY_NUMPAD_3) EACH(Numpad4, FLUXION_KEY_NUMPAD_4) EACH(Numpad5, FLUXION_KEY_NUMPAD_5) \
    EACH(Numpad6, FLUXION_KEY_NUMPAD_6) EACH(Numpad7, FLUXION_KEY_NUMPAD_7) EACH(Numpad8, FLUXION_KEY_NUMPAD_8) \
    EACH(Numpad9, FLUXION_KEY_NUMPAD_9)

#define FLUXION_SCRIPT_MOUSE_BUTTONS(EACH) \
    EACH(Left, FLUXION_MOUSE_BUTTON_LEFT) EACH(Right, FLUXION_MOUSE_BUTTON_RIGHT) EACH(Middle, FLUXION_MOUSE_BUTTON_MIDDLE) \
    EACH(X1, FLUXION_MOUSE_BUTTON_X1) EACH(X2, FLUXION_MOUSE_BUTTON_X2)

#define FLUXION_SCRIPT_GAMEPAD_BUTTONS(EACH) \
    EACH(A, FLUXION_GAMEPAD_BUTTON_A) EACH(B, FLUXION_GAMEPAD_BUTTON_B) EACH(X, FLUXION_GAMEPAD_BUTTON_X) \
    EACH(Y, FLUXION_GAMEPAD_BUTTON_Y) \
    EACH(LeftShoulder, FLUXION_GAMEPAD_BUTTON_LEFT_SHOULDER) EACH(RightShoulder, FLUXION_GAMEPAD_BUTTON_RIGHT_SHOULDER) \
    EACH(Back, FLUXION_GAMEPAD_BUTTON_BACK) EACH(Start, FLUXION_GAMEPAD_BUTTON_START) \
    EACH(LeftStick, FLUXION_GAMEPAD_BUTTON_LEFT_STICK) EACH(RightStick, FLUXION_GAMEPAD_BUTTON_RIGHT_STICK) \
    EACH(DpadUp, FLUXION_GAMEPAD_BUTTON_DPAD_UP) EACH(DpadDown, FLUXION_GAMEPAD_BUTTON_DPAD_DOWN) \
    EACH(DpadLeft, FLUXION_GAMEPAD_BUTTON_DPAD_LEFT) EACH(DpadRight, FLUXION_GAMEPAD_BUTTON_DPAD_RIGHT)

#define FLUXION_SCRIPT_GAMEPAD_AXES(EACH) \
    EACH(LeftX, FLUXION_GAMEPAD_AXIS_LEFT_X) EACH(LeftY, FLUXION_GAMEPAD_AXIS_LEFT_Y) \
    EACH(RightX, FLUXION_GAMEPAD_AXIS_RIGHT_X) EACH(RightY, FLUXION_GAMEPAD_AXIS_RIGHT_Y) \
    EACH(LeftTrigger, FLUXION_GAMEPAD_AXIS_LEFT_TRIGGER) EACH(RightTrigger, FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER)

std::string ConstantSets()
{
    std::string source;

#define FLUXION_SCRIPT_WRITE_SET(name, list) \
    source += "enum " name "\n{\n"; \
    list(FLUXION_SCRIPT_WRITE_CONSTANT) \
    source += "}\n\n";

#define FLUXION_SCRIPT_WRITE_CONSTANT(scriptName, engineValue) \
    source += "    " #scriptName " = " + std::to_string((int)(engineValue)) + ",\n";

    FLUXION_SCRIPT_WRITE_SET("KeyCode", FLUXION_SCRIPT_KEYS)
    FLUXION_SCRIPT_WRITE_SET("MouseButton", FLUXION_SCRIPT_MOUSE_BUTTONS)
    FLUXION_SCRIPT_WRITE_SET("GamepadButton", FLUXION_SCRIPT_GAMEPAD_BUTTONS)
    FLUXION_SCRIPT_WRITE_SET("GamepadAxis", FLUXION_SCRIPT_GAMEPAD_AXES)

#undef FLUXION_SCRIPT_WRITE_CONSTANT
#undef FLUXION_SCRIPT_WRITE_SET

    return source;
}

// The shaped forms of the two calls the engine can only take as separate
// numbers. They are written here, in the language, rather than bound:
// they are nothing but a Vector3 and a Color taken apart, so there is no
// reason for them to cross to the C side to be written.
const char* ShapedCalls()
{
    return
        "static class Draw\n"
        "{\n"
        "    static void Line(Vector3 from, Vector3 to, Color color)\n"
        "    {\n"
        "        DebugDraw.Line(from.x, from.y, from.z, to.x, to.y, to.z, color.r, color.g, color.b, color.a);\n"
        "    }\n"
        "\n"
        "    static void Triangle(Vector3 a, Vector3 b, Vector3 c, Color color)\n"
        "    {\n"
        "        DebugDraw.Triangle(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, color.r, color.g, color.b, color.a);\n"
        "    }\n"
        "\n"
        // A box drawn as its twelve edges, from the two corners furthest
        // apart. Written here because it is the same three numbers
        // rearranged twelve ways and nothing else.
        "    static void Box(Vector3 low, Vector3 high, Color color)\n"
        "    {\n"
        "        Vector3 a = new Vector3(low.x, low.y, low.z);\n"
        "        Vector3 b = new Vector3(high.x, low.y, low.z);\n"
        "        Vector3 c = new Vector3(high.x, low.y, high.z);\n"
        "        Vector3 d = new Vector3(low.x, low.y, high.z);\n"
        "        Vector3 e = new Vector3(low.x, high.y, low.z);\n"
        "        Vector3 f = new Vector3(high.x, high.y, low.z);\n"
        "        Vector3 g = new Vector3(high.x, high.y, high.z);\n"
        "        Vector3 h = new Vector3(low.x, high.y, high.z);\n"
        "\n"
        "        Line(a, b, color); Line(b, c, color); Line(c, d, color); Line(d, a, color);\n"
        "        Line(e, f, color); Line(f, g, color); Line(g, h, color); Line(h, e, color);\n"
        "        Line(a, e, color); Line(b, f, color); Line(c, g, color); Line(d, h, color);\n"
        "    }\n"
        "}\n"
        "\n"
        "static class MaterialValues\n"
        "{\n"
        "    static bool SetVector3(Material target, string name, Vector3 value)\n"
        "    {\n"
        "        return target.SetVec3(name, value.x, value.y, value.z);\n"
        "    }\n"
        "\n"
        "    static bool SetVector4(Material target, string name, Vector4 value)\n"
        "    {\n"
        "        return target.SetVec4(name, value.x, value.y, value.z, value.w);\n"
        "    }\n"
        "\n"
        "    static bool SetColor(Material target, string name, Color value)\n"
        "    {\n"
        "        return target.SetVec4(name, value.r, value.g, value.b, value.a);\n"
        "    }\n"
        "}\n";
}

// Which arguments name a constant of a set rather than any number that
// happens to be written. Said here because it is a decision about the
// interface: the C functions behind these take an ordinary number either
// way.
struct ConstantSetParameter
{
    const char* method;
    u32 parameter;
    const char* set;
};

const ConstantSetParameter kInputConstantSets[] = {
    { "IsKeyDown", 0, "KeyCode" },
    { "WasKeyPressed", 0, "KeyCode" },
    { "WasKeyReleased", 0, "KeyCode" },
    { "IsMouseButtonDown", 0, "MouseButton" },
    { "WasMouseButtonPressed", 0, "MouseButton" },
    { "WasMouseButtonReleased", 0, "MouseButton" },
    { "IsGamepadButtonDown", 1, "GamepadButton" },
    { "GamepadAxis", 1, "GamepadAxis" },
};

} // namespace

// --- The public C++ surface ----------------------------------------------

bool BuildEngineBindings(FluxionSceneHandle scene, BindingTable& table, DiagnosticList& outDiagnostics)
{
    const SourceLocation where{ "<engine>", 0, 0 };

    if (!Fluxion_Scene_IsValid(scene))
    {
        outDiagnostics.AddError(where, "there is no such scene for the engine's own types to be built against");
        return false;
    }
    s_scene = scene;

    EngineDescriptors descriptors;

    // A type may only take or answer with a handle to something the table
    // already holds, so the three things the host makes go in before what
    // names them, and what draws with them goes in last.
    const u32 mesh = AddBoundType(table, descriptors.MeshType(), nullptr, nullptr, outDiagnostics);
    const u32 material = AddBoundType(table, descriptors.MaterialType(), &ResolveMaterial, nullptr, outDiagnostics);
    const u32 pipeline = AddBoundType(table, descriptors.PipelineType(), nullptr, nullptr, outDiagnostics);
    const u32 assets = AddBoundType(table, descriptors.AssetsType(), nullptr, nullptr, outDiagnostics);
    const u32 time = AddBoundType(table, descriptors.TimeType(), nullptr, nullptr, outDiagnostics);
    const u32 input = AddBoundType(table, descriptors.InputType(), nullptr, nullptr, outDiagnostics);
    const u32 debugDraw = AddBoundType(table, descriptors.DebugDrawType(), nullptr, nullptr, outDiagnostics);
    const u32 renderer = AddBoundType(table, descriptors.RendererType(), nullptr, nullptr, outDiagnostics);

    if (mesh == kNoBoundType || material == kNoBoundType || pipeline == kNoBoundType || assets == kNoBoundType ||
        time == kNoBoundType || input == kNoBoundType || debugDraw == kNoBoundType || renderer == kNoBoundType)
    {
        return false;
    }

    for (const ConstantSetParameter& narrowed : kInputConstantSets)
    {
        if (BindParameterToConstantSet(table, input, narrowed.method, narrowed.parameter, narrowed.set)) continue;

        outDiagnostics.AddError(where, std::string("'Input.") + narrowed.method + "' cannot be told that argument " +
                                           std::to_string(narrowed.parameter + 1) + " names a constant of '" + narrowed.set + "'");
        return false;
    }

    return true;
}

const char* EnginePreludeSource()
{
    // Built once, the first time anyone asks: the sets are assembled from
    // the engine's own identities rather than written out, so there is
    // nothing to be gained by doing it more than once.
    static const std::string whole = ConstantSets() + ShapedCalls();
    return whole.c_str();
}

void SetScriptRenderer(FluxionRendererHandle renderer) { s_renderer = renderer; }

FluxionRendererHandle GetScriptRenderer(void) { return s_renderer; }

bool RegisterScriptMesh(const char* name, FluxionMeshBufferHandle mesh) { return s_meshes.Put(name, mesh); }

bool RegisterScriptMaterial(const char* name, FluxionMaterialHandle material) { return s_materials.Put(name, material); }

bool RegisterScriptPipeline(const char* name, FluxionRenderPipelineHandle pipeline) { return s_pipelines.Put(name, pipeline); }

void ClearScriptAssets(void)
{
    s_meshes.Clear();
    s_materials.Clear();
    s_pipelines.Clear();
}

} // namespace Fluxion::Scene
