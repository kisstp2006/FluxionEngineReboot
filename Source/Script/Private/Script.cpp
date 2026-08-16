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

#include <Fluxion/Script/Script.hpp>

#include <Compiler/Prelude.hpp>

#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Compiler/BytecodeEmitter.hpp>
#include <Fluxion/Script/Compiler/Lexer.hpp>
#include <Fluxion/Script/Compiler/Parser.hpp>
#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace Fluxion::Script
{

namespace
{

// The prelude is lexed on its own so its tokens keep their own file name:
// a message about it names the prelude and not the caller's source, which
// is what makes the two tellable apart afterwards.
std::vector<Token> LexWithPrelude(const std::string& source, const CompileOptions& options, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(PreludeSource(), PreludeSourceName(), diagnostics);

    // Only the last stream's end-of-file terminates the whole thing.
    if (!tokens.empty()) tokens.pop_back();

    if (!options.hostPrelude.empty())
    {
        std::vector<Token> hostTokens = Lex(options.hostPrelude, options.hostPreludeName, diagnostics);
        if (!hostTokens.empty()) hostTokens.pop_back();
        tokens.insert(tokens.end(), std::make_move_iterator(hostTokens.begin()), std::make_move_iterator(hostTokens.end()));
    }

    std::vector<Token> userTokens = Lex(source, options.fileName, diagnostics);
    tokens.insert(tokens.end(), std::make_move_iterator(userTokens.begin()), std::make_move_iterator(userTokens.end()));
    return tokens;
}

// True when anything went wrong inside the prelude itself. That is a
// defect in this module rather than a mistake in the source it was handed,
// so it is reported with a code of its own instead of being blamed on the
// caller.
bool BlamesThePrelude(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity != DiagnosticSeverity::Error) continue;
        if (entry.location.file == PreludeSourceName()) return true;
    }
    return false;
}

} // namespace

Fluxion::Foundation::Result<CompiledModule> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics)
{
    using ResultType = Fluxion::Foundation::Result<CompiledModule>;

    // Each step stops the pipeline with its own code, so a caller can
    // tell how far the source got without parsing the diagnostic text.
    // Code 9 is the odd one out: it says the failure was not the
    // caller's.
    std::vector<Token> tokens = LexWithPrelude(source, options, outDiagnostics);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always lex");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(1, "script lexing failed");
    }

    Program program = Parse(tokens, outDiagnostics);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always parse");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(2, "script parsing failed");
    }

    if (!Analyze(program, outDiagnostics, options.bindings))
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always analyze");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(3, "script semantic analysis failed");
    }

    CompiledModule module = Emit(program, options.fileName, options.moduleVersion, outDiagnostics, options.bindings);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always be emittable");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(4, "script bytecode emission failed");
    }

    return ResultType::Ok(std::move(module));
}

} // namespace Fluxion::Script
