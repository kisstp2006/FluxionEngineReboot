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

// `.rendergraph` file format (JSON):
//
//   {
//     "imports": [ { "name": "Backbuffer", "kind": "texture" } ],
//     "nodes": [
//       { "name": "brightness", "type": "CubeBrightnessCompute" },
//       { "name": "draw", "type": "CubeDraw" }
//     ]
//   }
//
// The format deliberately does NOT encode edges: each pass type's Setup
// callback declares the resource names it touches, and Compile derives
// the edges. The JSON only decides which pass instances exist and what
// they are named.
//
// "imports" is validated for shape but otherwise informational -- a real
// handle cannot come from a text file, so the caller still imports the
// resource through the C calls under the same name. The section records
// which names the file's author expects to be supplied that way.

#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <pdjson.h>

#include <string.h>

#define FLUXION_RENDER_GRAPH_JSON_MAX_NAME 64

typedef struct FluxionRenderGraphJsonNode
{
    char name[FLUXION_RENDER_GRAPH_JSON_MAX_NAME];
    char type[FLUXION_RENDER_GRAPH_JSON_MAX_NAME];
} FluxionRenderGraphJsonNode;

static void Fluxion_RenderGraphJson_CopyBoundedString(char* dest, usize destSize, const char* src, usize srcLength)
{
    usize copyLength = srcLength < destSize - 1 ? srcLength : destSize - 1;
    memcpy(dest, src, copyLength);
    dest[copyLength] = '\0';
}

static bool Fluxion_RenderGraphJson_SkipImportsArray(json_stream* json)
{
    enum json_type type = json_next(json);
    if (type != JSON_ARRAY) return false;

    while ((type = json_next(json)) == JSON_OBJECT)
    {
        bool hasName = false;
        bool hasKind = false;
        while ((type = json_next(json)) == JSON_STRING)
        {
            const char* key = json_get_string(json, NULL);
            if (strcmp(key, "name") == 0)
            {
                type = json_next(json);
                if (type != JSON_STRING) return false;
                hasName = true;
            }
            else if (strcmp(key, "kind") == 0)
            {
                type = json_next(json);
                if (type != JSON_STRING) return false;
                const char* value = json_get_string(json, NULL);
                if (strcmp(value, "texture") != 0 && strcmp(value, "buffer") != 0) return false;
                hasKind = true;
            }
            else
            {
                json_skip(json);
            }
        }
        if (type != JSON_OBJECT_END) return false;
        if (!hasName || !hasKind) return false;
    }

    return type == JSON_ARRAY_END;
}

// Parses the "nodes" array into outNodes (capacity maxNodes), validating
// every entry's "type" against the pass-type registry as it goes --
// returns false immediately (before adding anything to `graph`) on any
// malformed entry or unregistered type, per Fluxion_RenderGraph_LoadFromJSON's
// documented all-or-nothing contract.
static bool Fluxion_RenderGraphJson_ParseNodesArray(json_stream* json, FluxionRenderGraphJsonNode* outNodes, u32 maxNodes, u32* outNodeCount)
{
    enum json_type type = json_next(json);
    if (type != JSON_ARRAY) return false;

    u32 count = 0;
    while ((type = json_next(json)) == JSON_OBJECT)
    {
        if (count >= maxNodes) return false;

        FluxionRenderGraphJsonNode* node = &outNodes[count];
        node->name[0] = '\0';
        node->type[0] = '\0';

        while ((type = json_next(json)) == JSON_STRING)
        {
            const char* key = json_get_string(json, NULL);
            if (strcmp(key, "name") == 0)
            {
                type = json_next(json);
                if (type != JSON_STRING) return false;
                const char* value = json_get_string(json, NULL);
                Fluxion_RenderGraphJson_CopyBoundedString(node->name, sizeof(node->name), value, strlen(value));
            }
            else if (strcmp(key, "type") == 0)
            {
                type = json_next(json);
                if (type != JSON_STRING) return false;
                const char* value = json_get_string(json, NULL);
                Fluxion_RenderGraphJson_CopyBoundedString(node->type, sizeof(node->type), value, strlen(value));
            }
            else
            {
                json_skip(json);
            }
        }
        if (type != JSON_OBJECT_END) return false;

        if (node->name[0] == '\0' || node->type[0] == '\0') return false;
        if (!Fluxion_RenderGraphPassRegistry_Find(node->type)) return false;

        ++count;
    }
    if (type != JSON_ARRAY_END) return false;

    *outNodeCount = count;
    return true;
}

bool Fluxion_RenderGraph_LoadFromJSON(FluxionRenderGraph* graph, const char* jsonText, usize jsonLength)
{
    FLUXION_ASSERT(graph != NULL && jsonText != NULL);

    json_stream json;
    json_open_buffer(&json, jsonText, jsonLength);

    bool ok = true;
    enum json_type type = json_next(&json);
    if (type != JSON_OBJECT) ok = false;

    FluxionRenderGraphJsonNode s_parsedNodes[FLUXION_RENDER_GRAPH_MAX_NODES];
    u32 parsedNodeCount = 0;
    bool sawNodes = false;

    while (ok && (type = json_next(&json)) == JSON_STRING)
    {
        const char* key = json_get_string(&json, NULL);
        char keyBuffer[32];
        Fluxion_RenderGraphJson_CopyBoundedString(keyBuffer, sizeof(keyBuffer), key, strlen(key));

        if (strcmp(keyBuffer, "imports") == 0)
        {
            if (!Fluxion_RenderGraphJson_SkipImportsArray(&json)) { ok = false; break; }
        }
        else if (strcmp(keyBuffer, "nodes") == 0)
        {
            if (!Fluxion_RenderGraphJson_ParseNodesArray(&json, s_parsedNodes, FLUXION_RENDER_GRAPH_MAX_NODES, &parsedNodeCount)) { ok = false; break; }
            sawNodes = true;
        }
        else
        {
            json_skip(&json);
        }
    }

    if (ok && type != JSON_OBJECT_END) ok = false;
    if (ok && !sawNodes) ok = false;

    json_close(&json);

    if (!ok) return false;

    // Every entry already validated (well-formed + a registered pass
    // type) during the parse above -- this second pass only ever adds
    // nodes, it cannot fail.
    for (u32 i = 0; i < parsedNodeCount; ++i)
    {
        FluxionRenderGraphPassHandle handle = Fluxion_RenderGraph_AddPassFromRegistry(graph, s_parsedNodes[i].type, NULL);
        FLUXION_ASSERT(FLUXION_HANDLE_IS_VALID(handle));
        if (FLUXION_HANDLE_IS_VALID(handle))
        {
            Fluxion_RenderGraphJson_CopyBoundedString(graph->nodes[handle.index].name, sizeof(graph->nodes[handle.index].name), s_parsedNodes[i].name, strlen(s_parsedNodes[i].name));
        }
    }

    return true;
}
