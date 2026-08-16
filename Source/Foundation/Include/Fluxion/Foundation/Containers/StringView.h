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

#include <Fluxion/Foundation/Types.h>

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionStringView
{
    const char* data;
    usize length;
} FluxionStringView;

#define FLUXION_STRINGVIEW_NOT_FOUND ((usize)-1)

static inline FluxionStringView Fluxion_StringView_FromCStr(const char* cstr)
{
    FluxionStringView view;
    view.data = cstr;
    view.length = cstr ? strlen(cstr) : 0;
    return view;
}

static inline FluxionStringView Fluxion_StringView_Make(const char* data, usize length)
{
    FluxionStringView view;
    view.data = data;
    view.length = length;
    return view;
}

static inline bool Fluxion_StringView_Equals(FluxionStringView a, FluxionStringView b)
{
    if (a.length != b.length) return false;
    if (a.length == 0) return true;
    return memcmp(a.data, b.data, a.length) == 0;
}

static inline usize Fluxion_StringView_Find(FluxionStringView haystack, FluxionStringView needle)
{
    if (needle.length == 0 || needle.length > haystack.length)
    {
        return FLUXION_STRINGVIEW_NOT_FOUND;
    }

    usize last = haystack.length - needle.length;
    for (usize i = 0; i <= last; ++i)
    {
        if (memcmp(haystack.data + i, needle.data, needle.length) == 0)
        {
            return i;
        }
    }
    return FLUXION_STRINGVIEW_NOT_FOUND;
}

static inline FluxionStringView Fluxion_StringView_Substr(FluxionStringView view, usize start, usize length)
{
    FluxionStringView result;
    if (start >= view.length)
    {
        result.data = view.data + view.length;
        result.length = 0;
        return result;
    }
    usize available = view.length - start;
    result.data = view.data + start;
    result.length = length < available ? length : available;
    return result;
}

#ifdef __cplusplus
}
#endif
