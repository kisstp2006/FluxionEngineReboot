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
