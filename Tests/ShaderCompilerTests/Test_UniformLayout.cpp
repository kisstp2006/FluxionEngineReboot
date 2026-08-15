#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <string>

// Where a uniform-buffer member ends up, said the same way twice.
//
// The engine writes these buffers from the offsets the compiler reflects,
// and the shader reads them from wherever the emitted text puts them. If
// those two disagree, nothing fails: every value involved is an ordinary
// number, so what a shader reads as its roughness is simply whatever the
// engine wrote several parameters later. The picture is wrong and there
// is nothing to point at.
//
// So the emitted text carries the offsets EXPLICITLY -- packoffset in one
// language, layout(offset) in the other -- rather than letting each
// language pack the block its own way. These checks are the statement
// that it does.

namespace
{

using namespace Fluxion::ShaderCompiler;

// A matrix first, then scalars, then a three-component value. Chosen for
// what it catches: a rule that gave every member one sixteen-byte slot
// would place the second member on top of the matrix, and a rule that
// packed tightly would put the three scalars into one slot where the
// engine expects three.
const char* const kSource = R"(
[Uniform(Material)] Matrix4x4 transform;
[Uniform(Material)] float first;
[Uniform(Material)] float second;
[Uniform(Material)] Vector3 third;
[Uniform(Material)] Vector4 fourth;

[Target(0)] Vector4 fragColor;

void main() {
  Vector4 moved = transform * Vector4(first, second, 0.0, 1.0);
  return Vector4(moved.x + third.x, fourth.y, 0.0, 1.0);
}
)";

CompileOptions Options()
{
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<Test_UniformLayout>";
    return options;
}

bool Contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

void EveryMemberIsPlacedWhereTheReflectionSaysItIs(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = Compile(kSource, Options(), diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const CompiledShader& compiled = result.Value();

    // A matrix is FOUR sixteen-byte slots, so the member after it starts
    // at sixty-four and not at sixteen. This is the case that had nothing
    // exercising it for as long as no uniform block held a matrix and
    // anything else.
    const std::string& hlsl = compiled.hlslSource;
    TEST_CHECK(ctx, Contains(hlsl, "transform : packoffset(c0)"));
    TEST_CHECK(ctx, Contains(hlsl, "first : packoffset(c4)"));
    TEST_CHECK(ctx, Contains(hlsl, "second : packoffset(c5)"));
    TEST_CHECK(ctx, Contains(hlsl, "third : packoffset(c6)"));
    TEST_CHECK(ctx, Contains(hlsl, "fourth : packoffset(c7)"));

    // The same arrangement in the other language, in bytes rather than in
    // registers. Both are checked because the two languages pack a block
    // differently when left to themselves, and agreeing with the
    // reflection is exactly what stops that mattering.
    const std::string& glsl = compiled.glslSource;
    TEST_CHECK(ctx, Contains(glsl, "layout(offset = 0) mat4 transform"));
    TEST_CHECK(ctx, Contains(glsl, "layout(offset = 64) float first"));
    TEST_CHECK(ctx, Contains(glsl, "layout(offset = 80) float second"));
    TEST_CHECK(ctx, Contains(glsl, "layout(offset = 96) vec3 third"));
    TEST_CHECK(ctx, Contains(glsl, "layout(offset = 112) vec4 fourth"));

    // And the block says which layout those offsets are in. Without it
    // the offsets are still honoured but the rest of the block is
    // arranged however the driver likes, which is not something two
    // drivers have to agree about.
    TEST_CHECK(ctx, Contains(glsl, "layout(std140,"));
}

void TheReflectedSizeCoversEveryMember(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = Compile(kSource, Options(), diagnostics);
    if (!result.IsOk()) { TEST_CHECK(ctx, false); return; }

    // Five members: a matrix at four slots and four more at one each,
    // which is a hundred and twenty-eight bytes. A total that did not
    // cover the last member would size the engine's buffer too small, and
    // the write past its end is what a sanitizer build is here to notice.
    bool foundMaterial = false;
    for (const auto& buffer : result.Value().reflection.uniformBuffers)
    {
        if (buffer.group != BindingGroup::Material) continue;
        foundMaterial = true;
        TEST_CHECK(ctx, buffer.size == 128);

        for (const auto& member : buffer.members)
        {
            TEST_CHECK(ctx, member.offset + 16u <= buffer.size);
        }
    }
    TEST_CHECK(ctx, foundMaterial);
}

} // namespace

void Test_UniformLayout_Run(TestContext& ctx)
{
    EveryMemberIsPlacedWhereTheReflectionSaysItIs(ctx);
    TheReflectedSizeCoversEveryMember(ctx);
}
