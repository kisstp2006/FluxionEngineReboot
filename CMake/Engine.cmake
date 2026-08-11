include_guard(GLOBAL)

# Aggregates the engine's CMake build framework. Include order matters:
# EngineWarnings must be defined before EngineModule/EngineTest use it.
include("${CMAKE_CURRENT_LIST_DIR}/EngineCompiler.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineWarnings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineModule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineTest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineHeaderTest.cmake")
