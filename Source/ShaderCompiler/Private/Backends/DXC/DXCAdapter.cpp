#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#define FLUXION_POPEN _popen
#define FLUXION_PCLOSE _pclose
#else
#include <sys/wait.h>
#define FLUXION_POPEN popen
#define FLUXION_PCLOSE pclose
#endif

namespace Fluxion::ShaderCompiler
{

namespace
{

// dxc ships inside the Vulkan SDK's Bin directory -- preferred over a
// bare "dxc" PATH lookup since a machine can have the SDK installed
// without its Bin directory on PATH. Falls back to a bare "dxc" so a
// standalone DXC install (no full Vulkan SDK) still works.
std::string ResolveDXCCommand()
{
    const char* sdkPath = std::getenv("VULKAN_SDK");
    if (sdkPath && *sdkPath)
    {
        std::filesystem::path candidate = std::filesystem::path(sdkPath) / "Bin" /
#if defined(_WIN32)
            "dxc.exe";
#else
            "dxc";
#endif
        if (std::filesystem::exists(candidate))
            return "\"" + candidate.string() + "\"";
    }
    return "dxc";
}

const char* ProfileForStage(ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex: return "vs_6_0";
        case ShaderStage::Fragment: return "ps_6_0";
        case ShaderStage::Compute: return "cs_6_0";
        default: return "ps_6_0";
    }
}

std::filesystem::path MakeTempPath(const char* suffix)
{
    static std::atomic<unsigned int> counter{ 0 };
    unsigned int id = counter.fetch_add(1);
    auto nanos = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string name = "fluxion_shadercompiler_" + std::to_string(nanos) + "_" + std::to_string(id) + suffix;
    return std::filesystem::temp_directory_path() / name;
}

// Runs `command`, capturing combined stdout+stderr text (dxc's
// diagnostics go to stderr) and returning the process exit code.
int RunCaptured(const std::string& command, std::string& outLog)
{
    std::string fullCommand = command + " 2>&1";
#if defined(_WIN32)
    // _popen runs the command through `cmd.exe /c <command>`, which has a
    // long-standing quirk: if the command starts with a quoted token (our
    // quoted dxc path), cmd.exe mis-parses it unless the ENTIRE command
    // is wrapped in one more, outer pair of quotes.
    if (fullCommand.find('"') != std::string::npos)
        fullCommand = "\"" + fullCommand + "\"";
#endif
    FILE* pipe = FLUXION_POPEN(fullCommand.c_str(), "r");
    if (!pipe) return -1;

    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        outLog += buffer;

    int status = FLUXION_PCLOSE(pipe);
#if !defined(_WIN32)
    if (status != -1 && WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    return status;
}

} // namespace

bool IsDXCAvailable()
{
    std::string log;
    int status = RunCaptured(ResolveDXCCommand() + " --version", log);
    return status == 0;
}

Fluxion::Foundation::Result<std::vector<uint8_t>> CompileToSpirv(
    const std::string& hlslSource,
    ShaderStage stage,
    const std::string& entryPoint,
    DiagnosticList& outDiagnostics)
{
    std::filesystem::path inputPath = MakeTempPath(".hlsl");
    std::filesystem::path outputPath = MakeTempPath(".spv");

    {
        std::ofstream inputFile(inputPath, std::ios::binary);
        inputFile << hlslSource;
    }

    std::string command = ResolveDXCCommand() +
        " -spirv -fspv-target-env=vulkan1.2" +
        " -T " + ProfileForStage(stage) +
        " -E " + entryPoint +
        " -Fo \"" + outputPath.string() + "\"" +
        " \"" + inputPath.string() + "\"";

    std::string log;
    int status = RunCaptured(command, log);

    std::vector<uint8_t> spirv;
    if (status == 0 && std::filesystem::exists(outputPath))
    {
        std::ifstream outputFile(outputPath, std::ios::binary | std::ios::ate);
        std::streamsize size = outputFile.tellg();
        outputFile.seekg(0);
        spirv.resize((size_t)size);
        outputFile.read(reinterpret_cast<char*>(spirv.data()), size);
    }

    std::filesystem::remove(inputPath);
    std::filesystem::remove(outputPath);

    if (status != 0 || spirv.empty())
    {
        outDiagnostics.AddError(SourceLocation{ "dxc", 0, 0 }, "dxc invocation failed: " + log);
        return Fluxion::Foundation::Result<std::vector<uint8_t>>::Error(1, "dxc invocation failed");
    }

    return Fluxion::Foundation::Result<std::vector<uint8_t>>::Ok(std::move(spirv));
}

} // namespace Fluxion::ShaderCompiler
