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

#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionPropertyAccessKind
{
    FLUXION_PROPERTY_ACCESS_OFFSET = 0,
    FLUXION_PROPERTY_ACCESS_ACCESSOR,
} FluxionPropertyAccessKind;

// Fixed C signature every accessor-mode property's getter/setter must
// match, regardless of the property's actual type -- outValue/value point
// at storage of that actual type. The C++ facade (Reflection.hpp) builds
// these as trampolines wrapping real getter/setter member functions.
// `context` is whatever the property was registered with, and it is what
// lets one pair of functions serve many properties. A type whose fields
// are known when it is compiled needs none, and gets a distinct function
// per property from the C++ facade. A type whose fields are only known
// once the program runs -- a class read out of a script module, say --
// cannot have a function per field, so it registers one pair and tells
// them apart by this.
//
// Note what the two parameters separate: `instance` says WHICH object,
// `context` says WHICH field of it. Neither can stand in for the other.
typedef void (*FluxionPropertyGetterFn)(const void* instance, void* outValue, void* context);
typedef void (*FluxionPropertySetterFn)(void* instance, const void* value, void* context);

typedef struct FluxionPropertyInfo
{
    FluxionStringView name;
    FluxionTypeId type;
    usize size;
    FluxionPropertyFlags flags;
    FluxionPropertyAccessKind accessKind;

    // Anonymous union: `.offset` stays a direct field on
    // FluxionPropertyInfo (not `.something.offset`), so every existing
    // offset-mode call site keeps compiling unchanged -- this is a purely
    // additive extension, not a breaking change.
    union
    {
        usize offset;
        struct
        {
            FluxionPropertyGetterFn getter;
            FluxionPropertySetterFn setter;

            // Handed back to the two above, unread by anything else. The
            // property does not own it: whoever registered the type keeps
            // it alive for as long as the type is registered, the same
            // rule the registry already states for the descriptor itself.
            void* context;
        } accessor;
    };
} FluxionPropertyInfo;

#ifdef __cplusplus
}
#endif

// Builds one offset-mode FluxionPropertyInfo initializer for use inside a
// `static const FluxionPropertyInfo[] = { ... };` array. Plain
// brace-initialization (not a compound literal), so it works unchanged in
// both C and C++. The trailing `{ offsetof(Type, Field) }` initializes
// the anonymous union's first alternative (`offset`) positionally --
// deliberately not a designated initializer, since C++ doesn't allow
// designating a member of an anonymous union the way C does.
#define FLUXION_REFLECT_PROPERTY(Type, Field, PropertyTypeId, flags) \
    { \
        Fluxion_StringView_FromCStr(#Field), \
        (PropertyTypeId), \
        sizeof(((Type*)0)->Field), \
        (flags), \
        FLUXION_PROPERTY_ACCESS_OFFSET, \
        { offsetof(Type, Field) } \
    }
