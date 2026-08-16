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

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Assets/Package.h>
#include <Fluxion/Assets/VirtualFileSystem.h>

#include <utility>

// A C++ way of saying the same things the interface above says.
//
// One thing is added rather than restated, and it is the one worth
// adding: an asset that is held is let go of again. The C interface pairs
// Acquire with Release and leaves the pairing to whoever wrote the two
// calls; the type below does the second one when it goes out of scope,
// including on the way out of a function that returned early.

namespace Fluxion::Assets
{

// Holds a reference for as long as it exists.
//
// Copyable, and a copy is a second holder rather than a second asset --
// the same id acquired twice is one loaded thing with a count of two,
// which is what the loader already does.
class Asset
{
public:
    Asset() = default;

    explicit Asset(FluxionUUID id) : m_handle(Fluxion_Assets_Acquire(id)) {}
    explicit Asset(FluxionAssetRef ref) : m_handle(Fluxion_Assets_AcquireRef(ref)) {}

    Asset(const Asset& other) : m_handle(Fluxion_Assets_Acquire(Fluxion_Assets_GetId(other.m_handle))) {}

    Asset(Asset&& other) noexcept : m_handle(other.m_handle) { other.m_handle = Invalid(); }

    Asset& operator=(const Asset& other)
    {
        if (this != &other)
        {
            Asset copy(other);
            Swap(copy);
        }
        return *this;
    }

    Asset& operator=(Asset&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_handle = other.m_handle;
            other.m_handle = Invalid();
        }
        return *this;
    }

    ~Asset() { Release(); }

    void Swap(Asset& other) noexcept { std::swap(m_handle, other.m_handle); }

    void Release()
    {
        if (FLUXION_HANDLE_IS_VALID(m_handle))
        {
            Fluxion_Assets_Release(m_handle);
            m_handle = Invalid();
        }
    }

    FluxionAssetHandle Handle() const { return m_handle; }
    FluxionUUID Id() const { return Fluxion_Assets_GetId(m_handle); }
    FluxionAssetTypeId Type() const { return Fluxion_Assets_GetType(m_handle); }

    FluxionAssetState State() const { return Fluxion_Assets_GetState(m_handle); }
    bool IsReady() const { return State() == FLUXION_ASSET_STATE_READY; }
    bool HasFailed() const { return State() == FLUXION_ASSET_STATE_FAILED; }

    // True once the asking succeeded -- which is a different question
    // from whether the loading did.
    bool IsValid() const { return FLUXION_HANDLE_IS_VALID(m_handle); }
    explicit operator bool() const { return IsValid(); }

    FluxionAssetState Wait() { return Fluxion_Assets_Wait(m_handle); }

    // NULL until ready. The type is the caller's claim, not a checked
    // fact: an asset's object is whatever its own type made, and nothing
    // else here knows what that was.
    template<typename T>
    T* Get() const
    {
        return static_cast<T*>(Fluxion_Assets_GetObject(m_handle));
    }

private:
    static FluxionAssetHandle Invalid() { return FluxionAssetHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 }; }

    FluxionAssetHandle m_handle = Invalid();
};

} // namespace Fluxion::Assets
