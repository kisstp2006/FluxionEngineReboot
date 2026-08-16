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

// std::getenv is fine to call here (read-only, once, off any hot path),
// but MSVC's CRT flags it as deprecated in favor of _dupenv_s regardless
// -- this wraps the platform difference in one place rather than
// disabling the warning wholesale for the file.
std::string GetEnvironmentVariable(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

// dxc ships inside the Vulkan SDK's Bin directory -- preferred over a
// bare "dxc" PATH lookup since a machine can have the SDK installed
// without its Bin directory on PATH. Falls back to a bare "dxc" so a
// standalone DXC install (no full Vulkan SDK) still works.
std::string ResolveDXCCommand()
{
    std::string sdkPath = GetEnvironmentVariable("VULKAN_SDK");
    if (!sdkPath.empty())
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

std::string StagePrefix(ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex: return "vs";
        case ShaderStage::Fragment: return "ps";
        case ShaderStage::Compute: return "cs";
        default: return "ps";
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
    DiagnosticList& outDiagnostics,
    const DXCOptions& options)
{
    std::filesystem::path inputPath = MakeTempPath(".hlsl");
    std::filesystem::path outputPath = MakeTempPath(".spv");

    {
        std::ofstream inputFile(inputPath, std::ios::binary);
        inputFile << hlslSource;
    }

    std::string command = ResolveDXCCommand() +
        " -spirv -fspv-target-env=" + options.spirvTargetEnv +
        " -T " + StagePrefix(stage) + "_" + options.shaderModel +
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

Fluxion::Foundation::Result<std::vector<uint8_t>> CompileToDxil(
    const std::string& hlslSource,
    ShaderStage stage,
    const std::string& entryPoint,
    DiagnosticList& outDiagnostics,
    const DXILOptions& options)
{
    std::filesystem::path inputPath = MakeTempPath(".hlsl");
    std::filesystem::path outputPath = MakeTempPath(".dxil");

    {
        std::ofstream inputFile(inputPath, std::ios::binary);
        inputFile << hlslSource;
    }

    // No -spirv/-fspv-target-env here -- otherwise identical to
    // CompileToSpirv's command shape (same -T/-E/-Fo/input arguments),
    // just targeting dxc's native DXIL output instead.
    std::string command = ResolveDXCCommand() +
        " -T " + StagePrefix(stage) + "_" + options.shaderModel +
        " -E " + entryPoint +
        " -Fo \"" + outputPath.string() + "\"" +
        " \"" + inputPath.string() + "\"";

    std::string log;
    int status = RunCaptured(command, log);

    std::vector<uint8_t> dxil;
    if (status == 0 && std::filesystem::exists(outputPath))
    {
        std::ifstream outputFile(outputPath, std::ios::binary | std::ios::ate);
        std::streamsize size = outputFile.tellg();
        outputFile.seekg(0);
        dxil.resize((size_t)size);
        outputFile.read(reinterpret_cast<char*>(dxil.data()), size);
    }

    std::filesystem::remove(inputPath);
    std::filesystem::remove(outputPath);

    if (status != 0 || dxil.empty())
    {
        outDiagnostics.AddError(SourceLocation{ "dxc", 0, 0 }, "dxc invocation failed: " + log);
        return Fluxion::Foundation::Result<std::vector<uint8_t>>::Error(1, "dxc invocation failed");
    }

    return Fluxion::Foundation::Result<std::vector<uint8_t>>::Ok(std::move(dxil));
}

const std::string& DXCIdentity()
{
    // Asked once. Deliberately not remembered beyond this process: the
    // tool can be replaced while nothing is running, and the next run has
    // to notice that it was.
    static const std::string identity = []
    {
        std::string log;
        if (RunCaptured(ResolveDXCCommand() + " --version", log) != 0) return std::string();

        // Everything it said is kept; only trailing whitespace goes,
        // because that differs between platforms for no reason worth
        // treating as a different tool.
        while (!log.empty() && (log.back() == '\n' || log.back() == '\r' || log.back() == ' ')) log.pop_back();
        return log;
    }();
    return identity;
}

} // namespace Fluxion::ShaderCompiler
