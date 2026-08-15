#pragma once

#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>

// The engine's own shader library, and how a shader source reaches it.
//
// A shader in this engine does not restate the lighting maths, the
// surface description or the small helpers around them. It says
//
//     #include "Fluxion/Math.jsl"
//
// and the resolver below hands over the text. The compiler itself knows
// nothing about where any of it lives, which is why it takes a resolver
// rather than a search path.
//
// The text is built into the program (see engine_embed_shader_library),
// so there is no file to find at runtime and no way to run against a
// library from a different build.

namespace Fluxion::RenderCore
{

// Defined by the generated source. NULL when no library file goes by that
// name -- which the preprocessor reports as an unresolved include, naming
// the file and line that asked for it.
const char* FindShaderLibraryFile(const char* name);

// What the library contains, so a test can say that it contains anything
// at all. A generator that silently produced an empty table would leave
// every include failing, and the first symptom would be a shader error
// about something else entirely.
unsigned int GetShaderLibraryFileCount();
const char* GetShaderLibraryFileNameAt(unsigned int index);

// The resolver handed to every compilation this module starts.
//
// One function, not a captured lambda per call site: what a shader may
// include is a property of the engine, not of whoever happens to be
// compiling.
Fluxion::ShaderCompiler::IncludeResolver MakeShaderLibraryResolver();

} // namespace Fluxion::RenderCore
