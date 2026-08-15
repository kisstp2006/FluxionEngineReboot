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
