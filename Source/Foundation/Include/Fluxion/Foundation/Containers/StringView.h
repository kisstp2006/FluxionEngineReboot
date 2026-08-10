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
