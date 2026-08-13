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
        Test_Gc.cpp
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
        Fluxion/Script/Runtime/Vm.hpp
        Fluxion/Script/Script.hpp
)
