#pragma once

#include <Fluxion/Foundation/Defines.h>

#include <type_traits>
#include <utility>

namespace Fluxion::Foundation
{

// Classic RAII scope guard: runs `f` when the guard goes out of scope,
// unless Dismiss() was called first. Move-only (a copy would run the
// callback twice).
template<typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F f) : m_func(std::move(f)), m_active(true) {}

    ~ScopeExit()
    {
        if (m_active)
        {
            m_func();
        }
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ScopeExit(ScopeExit&& other) noexcept
        : m_func(std::move(other.m_func))
        , m_active(other.m_active)
    {
        other.m_active = false;
    }

    void Dismiss() { m_active = false; }

private:
    F m_func;
    bool m_active;
};

template<typename F>
ScopeExit<std::decay_t<F>> MakeScopeExit(F&& f)
{
    return ScopeExit<std::decay_t<F>>(std::forward<F>(f));
}

} // namespace Fluxion::Foundation

#define FLUXION_SCOPE_EXIT(...) \
    auto FLUXION_CONCAT(fluxionScopeExit_, __LINE__) = ::Fluxion::Foundation::MakeScopeExit([&]() { __VA_ARGS__; })
