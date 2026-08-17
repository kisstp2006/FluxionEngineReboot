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
    NAME RenderCoreTests
    SOURCES
        main.c
        Test_PassRegistry.c
        Test_GraphCompile.cpp
        Test_JsonLoader.c
        Test_DumpDot.c
        Test_ShaderProgram.cpp
        Test_ShaderReload.cpp
        Test_Material.cpp
        Test_MeshBuffer.c
        Test_MemoryDomains.c
        Test_RenderView.c
        Test_ShadowAtlas.c
        Test_ShadowMatrices.c
        Test_RendererFrame.cpp
        Test_MeshAsset.cpp
        Test_ShaderLibrary.cpp
        Test_SurfaceData.cpp
        Test_MaterialParameters.cpp
        Test_TextureAsset.cpp
        Test_BlockCompressGPU.cpp
        Test_LightingGPU.cpp
        Test_ComparisonSamplingGPU.cpp
        Test_ShadowPassGPU.cpp
        Test_Exposure.c
        Test_StandardMaterialCompiles.cpp
    DEPENDENCIES
        Fluxion::RenderCore
        Fluxion::RHI
        Fluxion::ShaderCompiler
        # The comparison below encodes blocks with this and then asks a
        # driver whether it agrees about what they say.
        Fluxion::TextureCompress
        # The shader reload checks start and stop a job system, to cover
        # both the arrangement a real host has and the one where nobody
        # started any workers at all.
        Fluxion::Core
)

engine_add_header_selfcontained_test(
    MODULE RenderCore
    HEADERS
        Fluxion/RenderCore/RenderGraph/RenderGraphHandles.h
        Fluxion/RenderCore/RenderGraph/RenderGraphResource.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPass.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h
        Fluxion/RenderCore/RenderGraph/RenderGraph.h
        Fluxion/RenderCore/Renderer/MeshBuffer.h
        Fluxion/RenderCore/Renderer/MeshAsset.h
        Fluxion/RenderCore/Renderer/MaterialShader.h
        Fluxion/RenderCore/Renderer/MaterialParameters.h
        Fluxion/RenderCore/Renderer/TextureAsset.h
)
