#include "ShaderLibrary.h"

namespace Fluxion::RenderCore
{

Fluxion::ShaderCompiler::IncludeResolver MakeShaderLibraryResolver()
{
    return [](const std::string& name, std::string& outContent) -> bool {
        const char* text = FindShaderLibraryFile(name.c_str());
        if (text == nullptr) return false;

        outContent = text;
        return true;
    };
}

} // namespace Fluxion::RenderCore
