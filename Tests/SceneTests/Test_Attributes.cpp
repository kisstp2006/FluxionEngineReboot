#include "SceneFixture.h"

#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneScript.hpp>

using namespace Fluxion::Script;

void Test_Attributes_Run(TestContext& ctx)
{
    // --- What a field says about itself is readable back ----------------

    {
        const char* const source =
            "class Tuned : Component\n"
            "{\n"
            "    [SerializeField] [Range(0, 10)] [Tooltip(\"how fast it turns\")]\n"
            "    float speed;\n"
            "\n"
            "    [Header(\"Appearance\"), SerializeField]\n"
            "    int layer;\n"
            "\n"
            "    int plain;\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "annotations-are-readable-back", source))
        {
            Vm* vm = run.Machine();
            const u32 tuned = FindClass(vm, "Tuned");
            TEST_CHECK(ctx, tuned != kNoClass);

            // Only what the class declared itself, in the order it was
            // written.
            TEST_CHECK(ctx, ClassFieldCount(vm, tuned) == 3);
            const FieldInfo* speed = ClassFieldAt(vm, tuned, 0);
            TEST_CHECK(ctx, speed != nullptr);
            if (speed)
            {
                TEST_CHECK(ctx, speed->name == "speed");
                TEST_CHECK(ctx, speed->type == ValueType::Float);
                TEST_CHECK(ctx, speed->attributes.size() == 3);
            }

            TEST_CHECK(ctx, FindClassField(vm, tuned, "layer") != nullptr);
            TEST_CHECK(ctx, FindClassField(vm, tuned, "nothing") == nullptr);

            TEST_CHECK(ctx, FindFieldAttribute(vm, tuned, "speed", "SerializeField") != nullptr);

            const Attribute* range = FindFieldAttribute(vm, tuned, "speed", "Range");
            TEST_CHECK(ctx, range != nullptr);
            if (range)
            {
                TEST_CHECK(ctx, range->arguments.size() == 2);
                TEST_CHECK(ctx, range->arguments[0].floatValue == 0.0f);
                TEST_CHECK(ctx, range->arguments[1].floatValue == 10.0f);
                TEST_CHECK(ctx, range->arguments[1].intValue == 10);
            }

            const Attribute* tooltip = FindFieldAttribute(vm, tuned, "speed", "Tooltip");
            TEST_CHECK(ctx, tooltip != nullptr);
            if (tooltip)
            {
                TEST_CHECK(ctx, tooltip->arguments.size() == 1);
                TEST_CHECK(ctx, tooltip->arguments[0].stringValue == "how fast it turns");
            }

            // Several in one pair of brackets is the same as several
            // pairs.
            const Attribute* header = FindFieldAttribute(vm, tuned, "layer", "Header");
            TEST_CHECK(ctx, header != nullptr);
            if (header) TEST_CHECK(ctx, header->arguments[0].stringValue == "Appearance");
            TEST_CHECK(ctx, FindFieldAttribute(vm, tuned, "layer", "SerializeField") != nullptr);

            // A field nothing was said about carries nothing.
            const FieldInfo* plain = FindClassField(vm, tuned, "plain");
            TEST_CHECK(ctx, plain != nullptr && plain->attributes.empty());
            TEST_CHECK(ctx, FindFieldAttribute(vm, tuned, "plain", "SerializeField") == nullptr);

            // A negative bound is written the way it reads.
            TEST_CHECK(ctx, ClassAttributeCount(vm, tuned) == 0);
        }
    }

    {
        const char* const source =
            "class Ranged : Component\n"
            "{\n"
            "    [Range(-2.5f, 2.5f)] float offset;\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "a-bound-may-be-negative", source))
        {
            Vm* vm = run.Machine();
            const Attribute* range = FindFieldAttribute(vm, FindClass(vm, "Ranged"), "offset", "Range");
            TEST_CHECK(ctx, range != nullptr);
            if (range)
            {
                TEST_CHECK(ctx, range->arguments[0].floatValue == -2.5f);
                TEST_CHECK(ctx, range->arguments[1].floatValue == 2.5f);
            }
        }
    }

    // --- What a class says it needs is enforced -------------------------

    {
        const char* const source =
            "class Body : Component\n"
            "{\n"
            "    int mass;\n"
            "    void Awake() { this.mass = 5; }\n"
            "    int Mass() { return this.mass; }\n"
            "}\n"
            "[RequireComponent(typeof(Body))]\n"
            "class Thruster : Component\n"
            "{\n"
            "    void Update(float deltaTime) { Console.WriteLine(\"Thruster:Update\"); }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "a-stated-requirement-is-enforced", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            Vm* vm = run.Machine();

            const u32 body = Fluxion::Scene::FindComponentClass(scene, "Body");
            const u32 thruster = Fluxion::Scene::FindComponentClass(scene, "Thruster");
            TEST_CHECK(ctx, body != kNoClass && thruster != kNoClass);

            // The requirement is in the module, naming the class it means
            // rather than the text that was written.
            const Attribute* requirement = FindClassAttribute(vm, thruster, "RequireComponent");
            TEST_CHECK(ctx, requirement != nullptr);
            if (requirement)
            {
                TEST_CHECK(ctx, requirement->arguments.size() == 1);
                TEST_CHECK(ctx, requirement->arguments[0].kind == AttributeArgumentKind::Class);
                TEST_CHECK(ctx, requirement->arguments[0].classIndex == body);
            }
            TEST_CHECK(ctx, FindClassAttribute(vm, body, "RequireComponent") == nullptr);

            FluxionGameObjectHandle bare = Fluxion_Scene_CreateGameObject(scene, "bare");

            // Refused, and said why: the missing one is not put there for
            // the caller.
            TEST_CHECK(ctx, Fluxion::Scene::AddComponent(scene, bare, thruster).IsNull());
            TEST_CHECK(ctx, Fluxion::Scene::ComponentCount(scene, bare) == 0);
            {
                const std::string reported = Fluxion_Scene_GetLastError(scene);
                TEST_CHECK(ctx, reported.find("Thruster") != std::string::npos);
                TEST_CHECK(ctx, reported.find("Body") != std::string::npos);
            }

            // With what it needs already there, it goes on.
            FluxionGameObjectHandle ready = Fluxion_Scene_CreateGameObject(scene, "ready");
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene, ready, body).IsNull());
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene, ready, thruster).IsNull());
            TEST_CHECK(ctx, Fluxion::Scene::ComponentCount(scene, ready) == 2);

            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() == "Thruster:Update");

            // And putting it on the bare object works once the missing one
            // has been added by hand.
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene, bare, body).IsNull());
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene, bare, thruster).IsNull());
        }
    }

    // --- Annotations the language does not have -------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        BindingTable bindings;
        DiagnosticList diagnostics;
        TEST_CHECK(ctx, Fluxion::Scene::BuildBindingTable(scene, bindings, diagnostics));

        CompileOptions options;
        options.fileName = "<annotations>";
        options.bindings = &bindings;
        options.hostPrelude = Fluxion::Scene::ComponentPreludeSource();

        // An unrecognized one is refused rather than carried along looking
        // as though something had read it.
        auto unknown = Compile("class A : Component { [NoSuchThing] int value; }\n", options, diagnostics);
        TEST_CHECK(ctx, !unknown.IsOk());

        DiagnosticList second;
        auto misplaced = Compile("[SerializeField] class B : Component { int value; }\n", options, second);
        TEST_CHECK(ctx, !misplaced.IsOk());

        DiagnosticList third;
        auto wrongCount = Compile("class C : Component { [Range(1)] int value; }\n", options, third);
        TEST_CHECK(ctx, !wrongCount.IsOk());

        DiagnosticList fourth;
        auto wrongKind = Compile("class D : Component { [Tooltip(3)] int value; }\n", options, fourth);
        TEST_CHECK(ctx, !wrongKind.IsOk());

        DiagnosticList fifth;
        auto unknownType = Compile("[RequireComponent(typeof(Nothing))] class E : Component { }\n", options, fifth);
        TEST_CHECK(ctx, !unknownType.IsOk());

        Fluxion_Scene_Destroy(scene);
    }
}
