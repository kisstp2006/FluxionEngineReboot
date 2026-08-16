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
    NAME AssetsTests
    SOURCES
        main.cpp
        Test_VirtualFileSystem.cpp
        Test_AssetTypes.cpp
        Test_AssetDatabase.cpp
        Test_AssetLoading.cpp
        Test_Package.cpp
        Test_AssetImport.cpp
        Test_AssetReload.cpp
    DEPENDENCIES
        Fluxion::Assets
        # These checks put files on disk and read the finished package
        # back as bytes, and they do it through the engine's own file
        # interface rather than the C library's -- the same way the thing
        # they are testing does.
        Fluxion::Platform
        # The import runtime hands its work to the job system.
        Fluxion::Core
    NO_EXCEPTIONS
    NO_RTTI
)

engine_add_header_selfcontained_test(
    MODULE Assets
    HEADERS
        Fluxion/Assets/AssetTypeId.h
        Fluxion/Assets/AssetType.h
        Fluxion/Assets/AssetRef.h
        Fluxion/Assets/AssetDatabase.h
        Fluxion/Assets/AssetService.h
        Fluxion/Assets/AssetSystem.h
        Fluxion/Assets/AssetImport.h
        Fluxion/Assets/Assets.h
        Fluxion/Assets/Package.h
        Fluxion/Assets/VirtualFileSystem.h
        Fluxion/Assets/Assets.hpp
)
