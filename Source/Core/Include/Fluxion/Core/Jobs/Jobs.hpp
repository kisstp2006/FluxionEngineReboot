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

#include <Fluxion/Core/Jobs/JobDesc.h>
#include <Fluxion/Core/Jobs/JobHandle.h>
#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Types.h>

#include <cstring>
#include <span>
#include <type_traits>

namespace Fluxion::Core::Jobs
{

namespace Detail
{
    // Bridges a C++ callable to FluxionJobFn's plain C function-pointer
    // shape -- same trampoline idiom Subsystem.hpp/Service.hpp/
    // Reflection.hpp already use for their own C++-to-C-ABI boundaries.
    template<typename F>
    struct CallableTrampoline
    {
        static void Invoke(void* data)
        {
            (*static_cast<F*>(data))();
        }
    };

    template<typename F>
    struct ParallelForTrampoline
    {
        static void Invoke(void* userData, u32 index)
        {
            // F (typically a capturing lambda) has no default constructor,
            // so this reinterprets userData's own storage as an F in
            // place rather than default-constructing one and memcpy-ing
            // into it -- valid because ParallelFor<F> only accepts F
            // that's trivially copyable and exactly pointer-sized.
            (*reinterpret_cast<F*>(&userData))(index);
        }
    };
}

// Copies the callable's bytes into the job pool's fixed-size inline
// buffer -- no heap allocation. F must be small enough to fit and
// trivially copyable (this is what job systems mean by "keep your
// captures small": pointers/indices/small PODs, not owning containers --
// Submit() never runs a destructor on the inline copy).
template<typename F>
FluxionJobHandle Submit(F&& function, std::span<const FluxionJobHandle> dependencies = {})
{
    using CallableT = std::decay_t<F>;
    static_assert(sizeof(CallableT) <= FLUXION_JOB_INLINE_DATA_SIZE,
        "job capture too large for the inline job data buffer -- keep captures small (pointers/indices/small PODs)");
    static_assert(std::is_trivially_copyable_v<CallableT>,
        "job capture must be trivially copyable -- Submit() copies it by value into the job pool and never runs a destructor on that copy");

    FluxionJobDesc desc{};
    desc.function = &Detail::CallableTrampoline<CallableT>::Invoke;
    desc.dataSize = sizeof(CallableT);
    std::memcpy(desc.data, &function, sizeof(CallableT));
    desc.dependencies = dependencies.data();
    desc.dependencyCount = static_cast<u32>(dependencies.size());

    return Fluxion_JobSystem_Submit(&desc);
}

inline FluxionJobHandle Wait(FluxionJobHandle handle)
{
    Fluxion_JobSystem_Wait(handle);
    return handle;
}

inline FluxionJobHandle CombineDependencies(std::span<const FluxionJobHandle> handles)
{
    return Fluxion_JobSystem_CombineDependencies(handles.data(), static_cast<u32>(handles.size()));
}

// The C ParallelFor API passes the callable through as a single void*
// userData, not through a per-job inline-copied buffer the way Submit
// does -- so unlike Submit<F>, the callable here must fit in one
// pointer's worth of storage (a captured pointer or reference is the
// common case: `ParallelFor(count, 64, [data](u32 i) { ... });`). For
// anything larger, capture a pointer to a struct holding the rest.
template<typename F>
FluxionJobHandle ParallelFor(u32 count, u32 batchSize, F&& function, std::span<const FluxionJobHandle> dependencies = {})
{
    using CallableT = std::decay_t<F>;
    static_assert(sizeof(CallableT) <= sizeof(void*),
        "ParallelFor's callable must fit in a single pointer-sized capture (e.g. one captured pointer/reference) -- "
        "capture a pointer to a struct holding more state instead");
    static_assert(std::is_trivially_copyable_v<CallableT>,
        "ParallelFor's callable must be trivially copyable");

    void* userData;
    std::memcpy(&userData, &function, sizeof(CallableT));

    return Fluxion_JobSystem_ParallelFor(count, batchSize, &Detail::ParallelForTrampoline<CallableT>::Invoke, userData,
        dependencies.data(), static_cast<u32>(dependencies.size()));
}

} // namespace Fluxion::Core::Jobs
