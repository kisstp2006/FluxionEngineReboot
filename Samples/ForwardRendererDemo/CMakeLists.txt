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

add_executable(ForwardRendererDemo main.cpp)
target_link_libraries(ForwardRendererDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation Fluxion::ShaderCompiler Fluxion::RenderCore Fluxion::Scene Fluxion::Script Fluxion::Core Fluxion::Assets)
engine_set_warnings(ForwardRendererDemo)

target_compile_definitions(ForwardRendererDemo PRIVATE
    FLUXION_DEMO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    FLUXION_DEMO_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
    # Under the build tree rather than beside the sources: a checkout is
    # never written to, and deleting the build tree is all it takes to be
    # rid of what has accumulated here.
    FLUXION_DEMO_SCRIPT_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ScriptCache"
    FLUXION_DEMO_SHADER_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ShaderCache"
    # Where the sample cooks its own sky to. Same reasoning as the two
    # caches above: written to at run time, so it lives where a build
    # lives and not where the sources do.
    FLUXION_DEMO_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}/Assets"
)

# The sample is the only thing that exercises a real device end to end --
# every test above it runs on the Null backend. A tree where each module
# passes its own checks and the assembled program does not start is one
# nothing else here would notice.
#
# Deliberately narrow: it draws a few frames and exits cleanly, which is
# enough to cover device and swapchain creation, shader compilation,
# pipeline creation, a real submit and present, and the whole shutdown
# path including saving the pipeline cache. It asserts nothing about what
# the frames look like -- that needs eyes, and a check that pretended
# otherwise would be worse than none.
add_test(NAME ForwardRendererDemoSmoke
         COMMAND ForwardRendererDemo --graphics=vulkan --frames=30)

# A machine with no GPU, no display, or no Vulkan loader cannot answer the
# question this asks, and failing there would mean "your environment"
# rather than "your change". The sample reports that case by exiting 77,
# which ctest reads as skipped.
set_tests_properties(ForwardRendererDemoSmoke PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120)
