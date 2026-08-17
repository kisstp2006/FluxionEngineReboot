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

// Building a graph straight from a `.rendergraph` text, with no asset
// system in between -- what a test and a small program want.
//
// The text itself is read by Fluxion_RenderGraphAsset_ParseText, which is
// where the format is documented. This call is what it was before that
// existed: NODES ONLY. The imports a file declares are a contract that
// needs real handles to be met, and there are none to be had here -- so
// they are read and validated for shape, and satisfying them is
// Fluxion_RenderGraphAsset_Instantiate's job, not this one's.

#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/RenderCore/Pipeline/RenderGraphAsset.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <string.h>

bool Fluxion_RenderGraph_LoadFromJSON(FluxionRenderGraph* graph, const char* jsonText, usize jsonLength)
{
    FLUXION_ASSERT(graph != NULL && jsonText != NULL);

    FluxionRenderGraphAsset asset;
    if (!Fluxion_RenderGraphAsset_ParseText(jsonText, jsonLength, &asset)) return false;

    // Checked before anything is added, so the all-or-nothing promise
    // holds for a file whose last node is the unregistered one.
    for (u32 i = 0; i < asset.nodeCount; ++i)
    {
        if (Fluxion_RenderGraphPassRegistry_Find(asset.nodes[i].passType) == NULL) return false;
    }

    for (u32 i = 0; i < asset.nodeCount; ++i)
    {
        const FluxionRenderGraphPassHandle handle = Fluxion_RenderGraph_AddPassFromRegistry(graph, asset.nodes[i].passType, NULL);
        FLUXION_ASSERT(FLUXION_HANDLE_IS_VALID(handle));
        if (!FLUXION_HANDLE_IS_VALID(handle)) return false;

        memcpy(graph->nodes[handle.index].name, asset.nodes[i].name, strlen(asset.nodes[i].name) + 1);
    }

    return true;
}
