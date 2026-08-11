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
