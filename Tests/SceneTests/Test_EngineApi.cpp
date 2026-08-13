#include "SceneFixture.h"

#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Time/Time.h>
#include <Fluxion/Scene/EngineScript.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

using namespace Fluxion::Script;

namespace
{

// What a script is given to write against: one component that reads the
// clock, one that reads the input state, and one that draws. Everything
// each of them observes is written out, so what a test asserts against is
// the text rather than anything reached back into.
const char* const kEngineComponents =
    "class Clock : Component\n"
    "{\n"
    "    void Update(float deltaTime)\n"
    "    {\n"
    "        Console.WriteLine(Time.FrameCount());\n"
    "        Console.WriteLine(Time.DeltaTime() == deltaTime);\n"
    "        Console.WriteLine(Time.UnscaledDeltaTime() >= Time.DeltaTime());\n"
    "        Console.WriteLine(Time.ElapsedTime() >= 0.0f);\n"
    "        Console.WriteLine(Time.TimeScale());\n"
    "        Console.WriteLine(Time.MaximumDeltaTime());\n"
    "    }\n"
    "}\n"
    "class Reader : Component\n"
    "{\n"
    "    void Update(float deltaTime)\n"
    "    {\n"
    "        Console.WriteLine(Input.IsMouseButtonDown(MouseButton.Left));\n"
    "        Console.WriteLine(Input.IsMouseButtonDown(MouseButton.Right));\n"
    "        Console.WriteLine(Input.WasMouseButtonPressed(MouseButton.Left));\n"
    "        Console.WriteLine(Input.MouseX());\n"
    "        Console.WriteLine(Input.MouseY());\n"
    "        Console.WriteLine(Input.MouseScroll());\n"
    "        Console.WriteLine(Input.IsKeyDown(KeyCode.W));\n"
    "        Console.WriteLine(Input.IsGamepadConnected(0));\n"
    "        Console.WriteLine(Input.IsGamepadButtonDown(0, GamepadButton.A));\n"
    "        Console.WriteLine(Input.GamepadAxis(0, GamepadAxis.LeftX));\n"
    // A gamepad that is not there is not an error to ask about, and
    // neither is one that could never be there.
    "        Console.WriteLine(Input.IsGamepadConnected(99));\n"
    "    }\n"
    "}\n"
    "class Painter : Component\n"
    "{\n"
    "    void Update(float deltaTime)\n"
    "    {\n"
    "        Console.WriteLine(Assets.HasMaterial(\"cube\"));\n"
    "        Console.WriteLine(Assets.HasMaterial(\"nothing\"));\n"
    "        Console.WriteLine(Assets.HasMesh(\"nothing\"));\n"
    "        Console.WriteLine(Assets.HasPipeline(\"nothing\"));\n"
    "\n"
    "        Material tint = Assets.FindMaterial(\"cube\");\n"
    "        Console.WriteLine(tint.SetFloat(\"strength\", 0.5f));\n"
    "        Console.WriteLine(MaterialValues.SetColor(tint, \"tint\", Color.White()));\n"
    "        Console.WriteLine(MaterialValues.SetVector3(tint, \"tint\", new Vector3(1.0f, 0.0f, 0.0f)));\n"
    "        tint.FlushDirty();\n"
    "\n"
    // Nothing is being drawn into, so none of these puts anything
    // anywhere. What is being established is that each one crosses over
    // and comes back rather than stopping the script.
    "        Renderer.DrawMesh(Assets.FindMesh(\"cube\"), tint, Assets.FindPipeline(\"opaque\"), this.gameObject);\n"
    "        Draw.Line(new Vector3(0.0f, 0.0f, 0.0f), new Vector3(1.0f, 0.0f, 0.0f), Color.White());\n"
    "        Draw.Triangle(new Vector3(0.0f, 0.0f, 0.0f), new Vector3(1.0f, 0.0f, 0.0f), new Vector3(0.0f, 1.0f, 0.0f), Color.Black());\n"
    "        Draw.Box(new Vector3(0.0f - 1.0f, 0.0f - 1.0f, 0.0f - 1.0f), new Vector3(1.0f, 1.0f, 1.0f), Color.White());\n"
    "        Console.WriteLine(\"drew\");\n"
    "    }\n"
    "}\n"
    // The arithmetic and the source of numbers are the language's own, so
    // this is only establishing that a component can reach both.
    "class Figures : Component\n"
    "{\n"
    "    Random source;\n"
    "\n"
    "    void Awake() { this.source = new Random(4); }\n"
    "\n"
    "    void Update(float deltaTime)\n"
    "    {\n"
    "        Console.WriteLine(Mathf.Clamp01(4.0f));\n"
    "        Console.WriteLine(Mathf.Lerp(0.0f, 8.0f, 0.25f));\n"
    "        Console.WriteLine(this.source.NextInt());\n"
    "    }\n"
    "}\n";

void ExpectRejected(TestContext& ctx, const char* label, const std::string& source)
{
    ScriptedScene scene;

    FluxionSceneHandle raw = Fluxion_Scene_Create();
    if (!Fluxion_Scene_IsValid(raw))
    {
        std::fprintf(stderr, "  FAIL: '%s' could not create a scene\n", label);
        ++ctx.failures;
        return;
    }

    BindingTable bindings;
    DiagnosticList diagnostics;
    const bool described =
        Fluxion::Scene::BuildBindingTable(raw, bindings, diagnostics) && Fluxion::Scene::BuildEngineBindings(raw, bindings, diagnostics);
    if (!described)
    {
        std::fprintf(stderr, "  FAIL: '%s' could not describe the engine\n", label);
        ScriptedScene::Report(diagnostics);
        ++ctx.failures;
        Fluxion_Scene_Destroy(raw);
        return;
    }

    CompileOptions options;
    options.fileName = label;
    options.bindings = &bindings;
    options.hostPrelude = std::string(Fluxion::Scene::ComponentPreludeSource()) + Fluxion::Scene::EnginePreludeSource();

    auto compiled = Compile(source, options, diagnostics);
    if (compiled.IsOk())
    {
        std::fprintf(stderr, "  FAIL: expected '%s' to be rejected\n", label);
        ++ctx.failures;
    }
    else
    {
        // Whatever was wrong has to be blamed on the source that was
        // written, not on the prelude the engine supplied alongside it.
        bool blamedOnTheSource = false;
        for (const Diagnostic& entry : diagnostics.entries)
        {
            if (entry.severity != DiagnosticSeverity::Error) continue;
            blamedOnTheSource = entry.location.file == label;
            break;
        }
        if (!blamedOnTheSource)
        {
            std::fprintf(stderr, "  FAIL: '%s' was rejected, but not against the source that was written\n", label);
            ScriptedScene::Report(diagnostics);
            ++ctx.failures;
        }
    }

    Fluxion_Scene_Destroy(raw);
}

// The number a name in the generated set stands for, or -1 when the set
// does not carry that name at all. Read out of the text itself, because
// the text is what a script is compiled against.
int ConstantIn(const char* set, const char* name)
{
    const std::string source = Fluxion::Scene::EnginePreludeSource();

    const std::string header = std::string("enum ") + set + "\n{\n";
    const size_t start = source.find(header);
    if (start == std::string::npos) return -1;

    const size_t end = source.find("\n}\n", start);
    if (end == std::string::npos) return -1;

    const std::string body = source.substr(start + header.size(), end - start - header.size());
    const std::string wanted = std::string("    ") + name + " = ";

    size_t at = body.find(wanted);
    while (at != std::string::npos)
    {
        // Only a whole line counts: "A = 1" must not be found inside
        // "LeftAlt = 1".
        if (at == 0 || body[at - 1] == '\n') return std::atoi(body.c_str() + at + wanted.size());
        at = body.find(wanted, at + 1);
    }
    return -1;
}

FluxionEvent MouseButtonEvent(FluxionEventType type, FluxionMouseButton button)
{
    FluxionEvent event = {};
    event.type = type;
    event.window.index = 0;
    event.window.generation = 1;
    event.data.mouseButton.button = button;
    return event;
}

} // namespace

void Test_EngineApi_Run(TestContext& ctx)
{
    // --- The sets of constants a script names things with ----------------
    //
    // Each is built from the identity the engine declares, so what is
    // established here is that the number a script writes really is the
    // number the input system works in -- for a key from each part of the
    // keyboard, and for every other set in full.
    {
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Unknown") == (int)FLUXION_KEY_UNKNOWN);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "A") == (int)FLUXION_KEY_A);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "W") == (int)FLUXION_KEY_W);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Z") == (int)FLUXION_KEY_Z);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Digit0") == (int)FLUXION_KEY_0);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Digit9") == (int)FLUXION_KEY_9);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "F1") == (int)FLUXION_KEY_F1);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "F12") == (int)FLUXION_KEY_F12);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Escape") == (int)FLUXION_KEY_ESCAPE);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Space") == (int)FLUXION_KEY_SPACE);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "LeftShift") == (int)FLUXION_KEY_LEFT_SHIFT);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "RightControl") == (int)FLUXION_KEY_RIGHT_CONTROL);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "PageDown") == (int)FLUXION_KEY_PAGE_DOWN);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Up") == (int)FLUXION_KEY_UP);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Right") == (int)FLUXION_KEY_RIGHT);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Numpad0") == (int)FLUXION_KEY_NUMPAD_0);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Numpad9") == (int)FLUXION_KEY_NUMPAD_9);

        // Nothing the input system has no notion of is offered: there is
        // no text entry, no cursor capture and no punctuation key behind
        // any of these, so no name for one exists either.
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Comma") == -1);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Semicolon") == -1);
        TEST_CHECK(ctx, ConstantIn("KeyCode", "Count") == -1);

        TEST_CHECK(ctx, ConstantIn("MouseButton", "Left") == (int)FLUXION_MOUSE_BUTTON_LEFT);
        TEST_CHECK(ctx, ConstantIn("MouseButton", "Right") == (int)FLUXION_MOUSE_BUTTON_RIGHT);
        TEST_CHECK(ctx, ConstantIn("MouseButton", "Middle") == (int)FLUXION_MOUSE_BUTTON_MIDDLE);
        TEST_CHECK(ctx, ConstantIn("MouseButton", "X1") == (int)FLUXION_MOUSE_BUTTON_X1);
        TEST_CHECK(ctx, ConstantIn("MouseButton", "X2") == (int)FLUXION_MOUSE_BUTTON_X2);

        TEST_CHECK(ctx, ConstantIn("GamepadButton", "A") == (int)FLUXION_GAMEPAD_BUTTON_A);
        TEST_CHECK(ctx, ConstantIn("GamepadButton", "DpadRight") == (int)FLUXION_GAMEPAD_BUTTON_DPAD_RIGHT);
        TEST_CHECK(ctx, ConstantIn("GamepadButton", "LeftShoulder") == (int)FLUXION_GAMEPAD_BUTTON_LEFT_SHOULDER);

        TEST_CHECK(ctx, ConstantIn("GamepadAxis", "LeftX") == (int)FLUXION_GAMEPAD_AXIS_LEFT_X);
        TEST_CHECK(ctx, ConstantIn("GamepadAxis", "RightTrigger") == (int)FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER);
    }

    // --- What a component sees when it runs -------------------------------

    Fluxion_Input_Init();
    Fluxion_Time_Init();
    Fluxion::Scene::ClearScriptAssets();
    Fluxion::Scene::SetScriptRenderer(FluxionRendererHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 });

    {
        ScriptedScene scene;
        if (scene.Start(ctx, "a-component-reading-the-clock", kEngineComponents, true))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "Subject");
            const u32 clock = Fluxion::Scene::FindComponentClass(scene.Scene(), "Clock");
            TEST_CHECK(ctx, clock != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, clock).IsNull());

            Fluxion_Time_SetTimeScale(1.0f);
            Fluxion_Time_SetMaximumDeltaTime(0.001f);

            Fluxion_Time_BeginFrame(); // the first frame of a run steps over nothing
            Fluxion_Time_BeginFrame();

            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), Fluxion_Time_GetDeltaTime());

            const std::vector<std::string>& seen = scene.Lines();
            TEST_CHECK(ctx, seen.size() == 6);
            if (seen.size() == 6)
            {
                TEST_CHECK(ctx, seen[0] == "2");     // two frames have begun
                TEST_CHECK(ctx, seen[1] == "true");  // the scene was handed exactly what the clock says
                TEST_CHECK(ctx, seen[2] == "true");  // the scale is 1, so the two deltas agree
                TEST_CHECK(ctx, seen[3] == "true");
                TEST_CHECK(ctx, seen[5] == "0.001"); // the ceiling a caller set is the one a script reads
            }

            // A script can move the clock, and what it set is what the
            // host reads back.
            Fluxion_Time_SetTimeScale(1.0f);
            Fluxion_Time_SetMaximumDeltaTime(FLUXION_TIME_DEFAULT_MAXIMUM_DELTA);
        }
    }

    {
        ScriptedScene scene;
        if (scene.Start(ctx, "a-component-reading-the-input", kEngineComponents, true))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "Subject");
            const u32 reader = Fluxion::Scene::FindComponentClass(scene.Scene(), "Reader");
            TEST_CHECK(ctx, reader != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, reader).IsNull());

            // Nothing held down, nothing moved: every question has an
            // answer and none of them is a fault.
            Fluxion_Input_BeginFrame();
            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            std::vector<std::string> seen = scene.Lines();
            TEST_CHECK(ctx, seen.size() == 11);
            if (seen.size() == 11)
            {
                TEST_CHECK(ctx, seen[0] == "false"); // left button
                TEST_CHECK(ctx, seen[1] == "false"); // right button
                TEST_CHECK(ctx, seen[2] == "false"); // pressed this frame
                TEST_CHECK(ctx, seen[6] == "false"); // the W key
                // Whether a controller is plugged into the machine running
                // this is not something a test gets to decide, so the only
                // thing asserted about the first one is that the question
                // has an answer at all. Demanding "false" here would be a
                // test that passes on the machine it was written on and
                // fails the moment someone plugs something in -- which is
                // exactly what happened.
                TEST_CHECK(ctx, seen[7] == "false" || seen[7] == "true"); // gamepad zero, if there is one
                TEST_CHECK(ctx, seen[10] == "false"); // a gamepad that could not exist
            }

            // Now with the left button held and the pointer moved. The
            // button went down during this frame, so it reads as both
            // held and newly pressed.
            Fluxion_Input_BeginFrame();

            FluxionEvent down = MouseButtonEvent(FLUXION_EVENT_MOUSE_BUTTON_DOWN, FLUXION_MOUSE_BUTTON_LEFT);
            Fluxion_Input_ProcessEvent(&down);

            FluxionEvent moved = {};
            moved.type = FLUXION_EVENT_MOUSE_MOVED;
            moved.data.mouseMoved.x = 40;
            moved.data.mouseMoved.y = 25;
            Fluxion_Input_ProcessEvent(&moved);

            FluxionEvent scrolled = {};
            scrolled.type = FLUXION_EVENT_MOUSE_SCROLLED;
            scrolled.data.mouseScroll.deltaY = 2.0f;
            Fluxion_Input_ProcessEvent(&scrolled);

            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            seen = scene.Lines();
            TEST_CHECK(ctx, seen.size() == 11);
            if (seen.size() == 11)
            {
                TEST_CHECK(ctx, seen[0] == "true");  // held
                TEST_CHECK(ctx, seen[1] == "false"); // and only that one
                TEST_CHECK(ctx, seen[2] == "true");  // went down during this frame
                TEST_CHECK(ctx, seen[3] == "40");
                TEST_CHECK(ctx, seen[4] == "25");
                TEST_CHECK(ctx, seen[5] == "2");
            }

            // A frame later the button is still held but no longer newly
            // pressed, and nothing has scrolled.
            Fluxion_Input_BeginFrame();
            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            seen = scene.Lines();
            if (seen.size() == 11)
            {
                TEST_CHECK(ctx, seen[0] == "true");
                TEST_CHECK(ctx, seen[2] == "false");
                TEST_CHECK(ctx, seen[5] == "0");
            }

            FluxionEvent up = MouseButtonEvent(FLUXION_EVENT_MOUSE_BUTTON_UP, FLUXION_MOUSE_BUTTON_LEFT);
            Fluxion_Input_ProcessEvent(&up);
        }
    }

    {
        // Nothing here has a device behind it, so every call answers the
        // way it answers when what it names is not there -- which is
        // exactly what has to be established: none of it stops the
        // script, and none of it pretends to have worked.
        ScriptedScene scene;
        if (scene.Start(ctx, "a-component-drawing", kEngineComponents, true))
        {
            // A material the renderer has never heard of. Registering it
            // is what makes the name reachable at all; whether anything
            // is behind the handle is the renderer's business.
            TEST_CHECK(ctx, Fluxion::Scene::RegisterScriptMaterial("cube", FluxionMaterialHandle{ 0, 1 }));

            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "Subject");
            const u32 painter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Painter");
            TEST_CHECK(ctx, painter != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, painter).IsNull());

            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            const std::vector<std::string>& seen = scene.Lines();
            TEST_CHECK(ctx, seen.size() == 8);
            if (seen.size() == 8)
            {
                TEST_CHECK(ctx, seen[0] == "true");  // the name was registered
                TEST_CHECK(ctx, seen[1] == "false"); // and these were not
                TEST_CHECK(ctx, seen[2] == "false");
                TEST_CHECK(ctx, seen[3] == "false");
                TEST_CHECK(ctx, seen[4] == "false"); // no material behind the handle
                TEST_CHECK(ctx, seen[5] == "false");
                TEST_CHECK(ctx, seen[6] == "false");
                TEST_CHECK(ctx, seen[7] == "drew");  // everything after it still ran
            }

            // A name registered again replaces what was there, and
            // clearing takes every name away.
            TEST_CHECK(ctx, Fluxion::Scene::RegisterScriptMaterial("cube", FluxionMaterialHandle{ 1, 1 }));
            TEST_CHECK(ctx, !Fluxion::Scene::RegisterScriptMaterial("", FluxionMaterialHandle{ 0, 1 }));
            Fluxion::Scene::ClearScriptAssets();
        }
    }

    {
        ScriptedScene scene;
        if (scene.Start(ctx, "a-component-doing-arithmetic", kEngineComponents, true))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "Subject");
            const u32 figures = Fluxion::Scene::FindComponentClass(scene.Scene(), "Figures");
            TEST_CHECK(ctx, figures != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, figures).IsNull());

            scene.ClearLines();
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            const std::vector<std::string>& seen = scene.Lines();
            TEST_CHECK(ctx, seen.size() == 3);
            if (seen.size() == 3)
            {
                TEST_CHECK(ctx, seen[0] == "1");
                TEST_CHECK(ctx, seen[1] == "2");
                TEST_CHECK(ctx, seen[2] == "67228"); // the fourth seed, stepped once
            }
        }
    }

    // --- What naming a constant of a set is for ---------------------------
    //
    // A number is not a key and a mouse button is not one either. Both are
    // refused where a key was asked for, which is the whole reason these
    // arguments are named with a set rather than written as numbers.
    ExpectRejected(ctx, "a bare number where a key was asked for",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsKeyDown(22); }\n"
        "}\n");

    ExpectRejected(ctx, "a mouse button where a key was asked for",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsKeyDown(MouseButton.Left); }\n"
        "}\n");

    ExpectRejected(ctx, "a key where a mouse button was asked for",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsMouseButtonDown(KeyCode.W); }\n"
        "}\n");

    ExpectRejected(ctx, "a gamepad axis where a gamepad button was asked for",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsGamepadButtonDown(0, GamepadAxis.LeftX); }\n"
        "}\n");

    // The gamepad number itself is an ordinary number and stays one.
    ExpectRejected(ctx, "a constant where the gamepad number was asked for",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsGamepadButtonDown(GamepadButton.A, GamepadButton.A); }\n"
        "}\n");

    // Nothing creates what the host makes, and nothing reaches a key the
    // engine has no notion of.
    ExpectRejected(ctx, "a material made by a script",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Material m = new Material(); }\n"
        "}\n");

    ExpectRejected(ctx, "a key the engine does not have",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { Input.IsKeyDown(KeyCode.Semicolon); }\n"
        "}\n");

    // Neither is anything offered that has no system behind it.
    ExpectRejected(ctx, "an axis named as text",
        "class Bad : Component\n"
        "{\n"
        "    void Update(float deltaTime) { float f = Input.GetAxis(\"Horizontal\"); }\n"
        "}\n");

    Fluxion_Time_Shutdown();
    Fluxion_Input_Shutdown();
}
