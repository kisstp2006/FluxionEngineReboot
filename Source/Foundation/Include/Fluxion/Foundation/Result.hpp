#pragma once

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Result.h>
#include <Fluxion/Foundation/Types.h>

#include <optional>
#include <utility>

namespace Fluxion::Foundation
{

// A value-carrying counterpart to the C FluxionResult -- an ergonomic C++
// wrapper, not a new engine-wide error model (FluxionResult in
// Foundation/Result.h remains the underlying status/diagnostics type,
// reachable via Status()). Never calls std::optional::value() (it
// throws) since Foundation is built with NO_EXCEPTIONS -- FLUXION_ASSERT
// guards misuse of Value() instead.
template<typename T>
class Result
{
public:
    static Result Ok(T value)
    {
        Result result;
        result.m_status = Fluxion_ResultOk();
        result.m_value.emplace(std::move(value));
        return result;
    }

    static Result Error(i32 code, const char* message)
    {
        Result result;
        result.m_status = Fluxion_ResultError(code, message);
        return result;
    }

    bool IsOk() const { return m_status.ok; }
    const FluxionResult& Status() const { return m_status; }

    const T& Value() const
    {
        FLUXION_ASSERT_MSG(IsOk(), "Result<T>::Value() called on an error result");
        return *m_value;
    }

    T& Value()
    {
        FLUXION_ASSERT_MSG(IsOk(), "Result<T>::Value() called on an error result");
        return *m_value;
    }

    const T* operator->() const { return &Value(); }
    T* operator->() { return &Value(); }

private:
    Result() = default;

    FluxionResult m_status{};
    std::optional<T> m_value;
};

// void specialization -- no value storage, just success/failure + diagnostics.
template<>
class Result<void>
{
public:
    static Result Ok()
    {
        Result result;
        result.m_status = Fluxion_ResultOk();
        return result;
    }

    static Result Error(i32 code, const char* message)
    {
        Result result;
        result.m_status = Fluxion_ResultError(code, message);
        return result;
    }

    bool IsOk() const { return m_status.ok; }
    const FluxionResult& Status() const { return m_status; }

private:
    Result() = default;

    FluxionResult m_status{};
};

} // namespace Fluxion::Foundation
