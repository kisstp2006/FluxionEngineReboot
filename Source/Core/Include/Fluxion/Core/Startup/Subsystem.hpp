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

#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>
#include <Fluxion/Foundation/Result.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <span>

namespace Fluxion::Core
{

// A subsystem is any type exposing this shape as static members -- no base
// class, no virtual dispatch, just a compile-time shape check. `Name`
// becomes the subsystem's stable FluxionSubsystemId via the same hashing
// FLUXION_SUBSYSTEM_ID_OF uses on the C side.
template<typename T>
concept SubsystemType = requires
{
    { T::Name } -> std::convertible_to<const char*>;
    { T::Startup() } -> std::same_as<FluxionResult>;
    { T::Shutdown() } -> std::same_as<void>;
};

namespace Detail
{
    template<SubsystemType T>
    struct SubsystemTrampoline
    {
        static FluxionResult Startup(void*) { return T::Startup(); }
        static void Shutdown(void*) { T::Shutdown(); }
    };
}

// Builds a FluxionSubsystemDesc from T and hands it to the C registry.
// Must be called explicitly (e.g. from an engine bootstrap function) --
// never from a namespace-scope static object's constructor, which would
// reintroduce the static initialization order problems the C kernel is
// specifically designed to avoid.
template<SubsystemType T>
bool RegisterSubsystem(FluxionStartupPhase phase, std::span<const FluxionSubsystemId> dependencies = {})
{
    FluxionSubsystemDesc desc{};
    desc.id = Fluxion_SubsystemId_FromName(Fluxion_StringView_FromCStr(T::Name));
    desc.name = T::Name;
    desc.phase = phase;
    desc.dependencies = dependencies.data();
    desc.dependencyCount = static_cast<u32>(dependencies.size());
    desc.startup = &Detail::SubsystemTrampoline<T>::Startup;
    desc.shutdown = &Detail::SubsystemTrampoline<T>::Shutdown;
    desc.userdata = nullptr;
    return Fluxion_SubsystemRegistry_Register(&desc);
}

} // namespace Fluxion::Core
