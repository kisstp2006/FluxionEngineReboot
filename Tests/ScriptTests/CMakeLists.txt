# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

engine_add_test(
    NAME ScriptTests
    SOURCES
        main.cpp
        Test_Lexer.cpp
        Test_Parser.cpp
        Test_Semantic.cpp
        Test_Bytecode.cpp
        Test_VmExecution.cpp
        Test_Objects.cpp
        Test_Structs.cpp
        Test_Casts.cpp
        Test_Enums.cpp
        Test_Mathf.cpp
        Test_Generics.cpp
        Test_Arrays.cpp
        Test_Gc.cpp
        Test_Binding.cpp
        Test_StackTrace.cpp
        Test_Serialization.cpp
    DEPENDENCIES
        Fluxion::Script
    NO_EXCEPTIONS
    NO_RTTI
)

target_compile_definitions(ScriptTests PRIVATE
    FLUXION_TEST_SCRIPT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
)

engine_add_header_selfcontained_test(
    MODULE Script
    HEADERS
        Fluxion/Script/Diagnostics.hpp
        Fluxion/Script/Compiler/Token.hpp
        Fluxion/Script/Compiler/Lexer.hpp
        Fluxion/Script/Compiler/Ast.hpp
        Fluxion/Script/Compiler/Parser.hpp
        Fluxion/Script/Compiler/Semantic.hpp
        Fluxion/Script/Compiler/BytecodeEmitter.hpp
        Fluxion/Script/Runtime/Value.hpp
        Fluxion/Script/Runtime/Bytecode.hpp
        Fluxion/Script/Runtime/Binding.hpp
        Fluxion/Script/Runtime/ModuleSerializer.hpp
        Fluxion/Script/Runtime/CompileCache.hpp
        Fluxion/Script/Runtime/Vm.hpp
        Fluxion/Script/Script.hpp
        Fluxion/Script/ScriptRuntimeService.hpp
)
