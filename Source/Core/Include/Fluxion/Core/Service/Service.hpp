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

#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace Fluxion::Core
{

// A service interface is any standard-layout type exposing `Name`/
// `Version` as static members and embedding a FluxionServiceHeader (named
// `header`) as its first member -- RegisterService fills that header in
// from T::Name/T::Version/sizeof(T); the offset-0 requirement is what
// lets Fluxion_ServiceRegistry_Register read it back out of a bare
// `const void*` on the C side.
template<typename T>
concept ServiceType = requires
{
    { T::Name } -> std::convertible_to<const char*>;
    { T::Version } -> std::convertible_to<u32>;
} && std::is_standard_layout_v<T>;

template<ServiceType T>
bool RegisterService(T& interfaceInstance)
{
    static_assert(offsetof(T, header) == 0, "service interface must embed FluxionServiceHeader as its first member, named `header`");

    interfaceInstance.header.serviceId = Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name));
    interfaceInstance.header.version = T::Version;
    interfaceInstance.header.structSize = sizeof(T);
    return Fluxion_ServiceRegistry_Register(&interfaceInstance);
}

template<ServiceType T>
void UnregisterService()
{
    Fluxion_ServiceRegistry_Unregister(Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name)));
}

template<ServiceType T>
const T* GetService()
{
    const FluxionServiceId id = Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(T::Name));
    return static_cast<const T*>(Fluxion_ServiceRegistry_Get(id, T::Version));
}

} // namespace Fluxion::Core
