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

set(RHITESTS_SOURCES
    main.c
    Test_BackendAvailability.c
    Test_Handles.c
    Test_Format.c
    Test_CompressedUpload.c
    Test_Cubemap.c
    Test_Capabilities.c
    Test_NullBackend.c
    Test_NativeHandle.c
    Test_VulkanBackend.c
    Test_OpenGLBackend.c
    Test_BindGroup.c
    Test_PipelineCacheFile.cpp
    Test_Timestamps.c
    Test_Validation.c
)
if(WIN32)
    # D3D12 doesn't exist outside Windows -- same reasoning as
    # Source/RHI/CMakeLists.txt excluding Private/D3D12 elsewhere.
    list(APPEND RHITESTS_SOURCES Test_D3D12Backend.c)
endif()

engine_add_test(
    NAME RHITests
    SOURCES ${RHITESTS_SOURCES}
    DEPENDENCIES
        Fluxion::RHI
)

engine_add_header_selfcontained_test(
    MODULE RHI
    HEADERS
        Fluxion/RHI/Handles.h
        Fluxion/RHI/Capabilities.h
        Fluxion/RHI/Adapter.h
        Fluxion/RHI/Format.h
        Fluxion/RHI/ResourceState.h
        Fluxion/RHI/ResourceDesc.h
        Fluxion/RHI/NativeHandle.h
        Fluxion/RHI/RHI.h
)
