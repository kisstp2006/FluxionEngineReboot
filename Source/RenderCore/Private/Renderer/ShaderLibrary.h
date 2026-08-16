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
