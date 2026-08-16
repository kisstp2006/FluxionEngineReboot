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

#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

#define FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND ((usize)-1)

static FluxionRenderGraphPassType s_passTypes[FLUXION_RENDER_GRAPH_MAX_PASS_TYPES];
static usize s_passTypeCount = 0;
static bool s_passRegistryInitialized = false;

void Fluxion_RenderGraphPassRegistry_Init(void)
{
    FLUXION_ASSERT_MSG(!s_passRegistryInitialized, "Fluxion_RenderGraphPassRegistry_Init called twice without a Shutdown in between");
    s_passTypeCount = 0;
    s_passRegistryInitialized = true;
}

void Fluxion_RenderGraphPassRegistry_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_passRegistryInitialized, "Fluxion_RenderGraphPassRegistry_Shutdown called before Init");
    s_passTypeCount = 0;
    s_passRegistryInitialized = false;
}

static usize Fluxion_FindPassTypeIndex(const char* name)
{
    for (usize i = 0; i < s_passTypeCount; ++i)
    {
        if (strcmp(s_passTypes[i].name, name) == 0)
        {
            return i;
        }
    }
    return FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND;
}

bool Fluxion_RenderGraphPassRegistry_Register(const FluxionRenderGraphPassType* passType)
{
    FLUXION_ASSERT(s_passRegistryInitialized);
    FLUXION_ASSERT(passType != NULL);
    FLUXION_ASSERT(passType->name != NULL);

    if (s_passTypeCount >= FLUXION_RENDER_GRAPH_MAX_PASS_TYPES) return false;
    if (Fluxion_FindPassTypeIndex(passType->name) != FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return false;

    s_passTypes[s_passTypeCount] = *passType;
    ++s_passTypeCount;
    return true;
}

void Fluxion_RenderGraphPassRegistry_Unregister(const char* name)
{
    FLUXION_ASSERT(s_passRegistryInitialized);

    usize index = Fluxion_FindPassTypeIndex(name);
    if (index == FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return;

    // Swap-with-last removal -- registration order carries no meaning here.
    s_passTypes[index] = s_passTypes[s_passTypeCount - 1];
    --s_passTypeCount;
}

const FluxionRenderGraphPassType* Fluxion_RenderGraphPassRegistry_Find(const char* name)
{
    FLUXION_ASSERT(s_passRegistryInitialized);

    usize index = Fluxion_FindPassTypeIndex(name);
    if (index == FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return NULL;

    return &s_passTypes[index];
}
