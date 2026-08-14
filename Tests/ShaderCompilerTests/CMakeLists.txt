engine_add_test(
    NAME ShaderCompilerTests
    SOURCES
        main.cpp
        Test_Lexer.cpp
    Test_Preprocessor.cpp
        Test_Parser.cpp
        Test_Semantic.cpp
        Test_HLSLBackend.cpp
        Test_GLSLBackend.cpp
        Test_DXCAdapter.cpp
        Test_Compatibility.cpp
        Test_ShaderCache.cpp
        Test_BindingGroups.cpp
        Test_ComputeStorageBuffer.cpp
    DEPENDENCIES
        Fluxion::ShaderCompiler
    NO_EXCEPTIONS
    NO_RTTI
)

target_compile_definitions(ShaderCompilerTests PRIVATE
    FLUXION_TEST_SHADERCOMPILER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
)

engine_add_header_selfcontained_test(
    MODULE ShaderCompiler
    HEADERS
        Fluxion/ShaderCompiler/Diagnostics.hpp
        Fluxion/ShaderCompiler/Frontend/Token.hpp
        Fluxion/ShaderCompiler/Frontend/Lexer.hpp
        Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp
        Fluxion/ShaderCompiler/Frontend/Parser.hpp
        Fluxion/ShaderCompiler/AST/Ast.hpp
        Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp
        Fluxion/ShaderCompiler/IR/ShaderIR.hpp
        Fluxion/ShaderCompiler/Reflection.hpp
        Fluxion/ShaderCompiler/Backends/HLSL/HLSLBackend.hpp
        Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp
        Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp
        Fluxion/ShaderCompiler/ShaderCompiler.hpp
)
