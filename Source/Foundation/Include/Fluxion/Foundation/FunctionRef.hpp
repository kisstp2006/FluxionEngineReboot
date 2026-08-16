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
