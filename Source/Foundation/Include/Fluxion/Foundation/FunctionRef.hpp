#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace Fluxion::Foundation
{

template<typename Signature>
class FunctionRef; // primary template intentionally undefined

// Non-owning, type-erased view over any callable matching R(Args...) --
// no heap allocation, and deliberately not std::function (which can
// throw bad_function_call; Foundation is built NO_EXCEPTIONS, so this is
// the alternative). The referenced callable must outlive the FunctionRef,
// same contract as any other non-owning reference type.
template<typename R, typename... Args>
class FunctionRef<R(Args...)>
{
public:
    template<typename F>
    FunctionRef(F&& callable) noexcept
        : m_callable(const_cast<void*>(static_cast<const void*>(std::addressof(callable))))
        , m_invoke(&Invoke<std::remove_reference_t<F>>)
    {
    }

    R operator()(Args... args) const
    {
        return m_invoke(m_callable, std::forward<Args>(args)...);
    }

private:
    template<typename F>
    static R Invoke(void* callable, Args... args)
    {
        return (*static_cast<F*>(callable))(std::forward<Args>(args)...);
    }

    void* m_callable;
    R (*m_invoke)(void*, Args...);
};

} // namespace Fluxion::Foundation
