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

include(EngineShaderLibrary)

# The engine's own shader library, carried in the binary rather than
# looked for at runtime. Order matters here only in that the generated
# source has to exist before the module claims it.
engine_embed_shader_library(
    OUTPUT FLUXION_SHADER_LIBRARY_GENERATED
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    FILES
        Fluxion/Math.jsl
        Fluxion/Surface.jsl
        Fluxion/Frame.jsl
        Fluxion/SphericalHarmonics.jsl
        Fluxion/CubeMapping.jsl
        Fluxion/ImportanceSampling.jsl
        Fluxion/Object.jsl
        Fluxion/Varyings.jsl
        Fluxion/BRDF.jsl
        Fluxion/Lighting.jsl
        Fluxion/Tonemap.jsl
        Fluxion/Material.jsl
        Fluxion/Pass/Vertex.jsl
        Fluxion/Pass/GPUCull.jsl
        Fluxion/Pass/IrradianceProject.jsl
        Fluxion/Pass/FullscreenVertex.jsl
        Fluxion/Pass/DepthPyramid.jsl
        Fluxion/Pass/MotionVector.jsl
        Fluxion/Pass/MotionVectorWrite.jsl
        Fluxion/Pass/ShadowDepth.jsl
        Fluxion/Pass/ShadowDepthWrite.jsl
        Fluxion/Pass/SpecularPrefilter.jsl
        Fluxion/Pass/DfgIntegrate.jsl
        Fluxion/Pass/Forward.jsl
        Fluxion/Pass/DepthOnly.jsl
        Fluxion/Pass/SkyboxVertex.jsl
        Fluxion/Pass/Skybox.jsl
)

engine_add_module(
    NAME RenderCore
    TYPE STATIC
    LANGUAGE C CXX
    PUBLIC_DEPENDENCIES
        Fluxion::RHI
        Fluxion::Foundation
        # MeshAsset.h names an asset type by its id, so whoever includes
        # it sees the asset headers too.
        Fluxion::Assets
    PRIVATE_DEPENDENCIES
        pdjson
        Fluxion::ShaderCompiler
        # For the job system, so a shader reload's compiling can happen off
        # the frame. Private: nothing in this module's headers mentions a
        # job, and a caller does not have to know one was used.
        Fluxion::Core
)

# Added after the module rather than through its SOURCES, so the auto-glob
# of Private/ keeps working for everything else.
target_sources(RenderCore PRIVATE "${FLUXION_SHADER_LIBRARY_GENERATED}")
