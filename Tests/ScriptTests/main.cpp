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

#include "TestFramework.h"

#include <cstdio>

void Test_Lexer_Run(TestContext& ctx);
void Test_Parser_Run(TestContext& ctx);
void Test_Semantic_Run(TestContext& ctx);
void Test_Bytecode_Run(TestContext& ctx);
void Test_VmExecution_Run(TestContext& ctx);
void Test_VmExecution_Fixture_Run(TestContext& ctx);
void Test_Objects_Run(TestContext& ctx);
void Test_Structs_Run(TestContext& ctx);
void Test_Casts_Run(TestContext& ctx);
void Test_Enums_Run(TestContext& ctx);
void Test_Mathf_Run(TestContext& ctx);
void Test_Generics_Run(TestContext& ctx);
void Test_Arrays_Run(TestContext& ctx);
void Test_Gc_Run(TestContext& ctx);
void Test_Binding_Run(TestContext& ctx);
void Test_StackTrace_Run(TestContext& ctx);
void Test_Serialization_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running ScriptTests...\n");

    Test_Lexer_Run(ctx);
    Test_Parser_Run(ctx);
    Test_Semantic_Run(ctx);
    Test_Bytecode_Run(ctx);
    Test_VmExecution_Run(ctx);
    Test_VmExecution_Fixture_Run(ctx);
    Test_Objects_Run(ctx);
    Test_Structs_Run(ctx);
    Test_Casts_Run(ctx);
    Test_Enums_Run(ctx);
    Test_Mathf_Run(ctx);
    Test_Generics_Run(ctx);
    Test_Arrays_Run(ctx);
    Test_Gc_Run(ctx);
    Test_Binding_Run(ctx);
    Test_StackTrace_Run(ctx);
    Test_Serialization_Run(ctx);

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All ScriptTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d ScriptTests check(s) failed.\n", ctx.failures);
    return 1;
}
