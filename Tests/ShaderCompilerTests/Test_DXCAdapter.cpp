#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstdio>

using namespace Fluxion::ShaderCompiler;

void Test_DXCAdapter_Run(TestContext& ctx)
{
    if (!IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping DXC adapter checks\n");
        return;
    }

    const char* hlsl =
        "struct PSOutput { float4 color : SV_Target0; };\n"
        "PSOutput main() { PSOutput o; o.color = float4(1.0, 0.0, 0.0, 1.0); return o; }\n";

    DiagnosticList diagnostics;
    auto result = CompileToSpirv(hlsl, ShaderStage::Fragment, "main", diagnostics);
    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
            std::fprintf(stderr, "  dxc: %s\n", d.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
    if (result.IsOk())
    {
        TEST_CHECK(ctx, !result.Value().empty());
        // SPIR-V binaries start with the magic number 0x07230203.
        TEST_CHECK(ctx, result.Value().size() >= 4 &&
            result.Value()[0] == 0x03 && result.Value()[1] == 0x02 &&
            result.Value()[2] == 0x23 && result.Value()[3] == 0x07);
    }
}
