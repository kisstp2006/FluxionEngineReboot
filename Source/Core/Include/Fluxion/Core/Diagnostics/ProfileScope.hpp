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

#pragma once

#include <Fluxion/Core/Diagnostics/Profiler.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>

namespace Fluxion::Core
{

// RAII scope guard: begins a profiler zone on construction, ends it on
// destruction (MemoryScope.hpp's pattern). Always constructed as a named
// local via FLUXION_PROFILE_SCOPE/FLUXION_PROFILE_FUNCTION, never
// returned from a function, so it doesn't need to be movable -- copy and
// move are both deleted to rule out accidental double-begin/end.
class ProfileScope
{
public:
    ProfileScope(const char* name, const char* file, u32 line, const char* function)
        : m_location{ file, function, line }
    {
        Fluxion_Profiler_ZoneBegin(&m_location, name);
    }

    ~ProfileScope()
    {
        Fluxion_Profiler_ZoneEnd();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;

private:
    FluxionSourceLocation m_location;
};

} // namespace Fluxion::Core

#define FLUXION_PROFILE_SCOPE(name) \
    ::Fluxion::Core::ProfileScope FLUXION_CONCAT(fluxionProfileScope_, __LINE__)(name, __FILE__, __LINE__, __func__)

#define FLUXION_PROFILE_FUNCTION() FLUXION_PROFILE_SCOPE(__func__)
