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

// The authored `.rendergraph` reader. The format itself is documented
// alongside Fluxion_RenderGraphAsset_ParseText.
//
// Separate from the cooked reader beside it because the two are different
// jobs: this one runs where a project is being edited and is allowed to
// be slow and talkative, the other runs when a game starts and is neither.

#include <Fluxion/RenderCore/Pipeline/RenderGraphAsset.h>

#include <Fluxion/Foundation/Log.h>

#include <pdjson.h>

#include <string.h>

#define FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY "RenderGraphAsset"

// A name that does not fit is refused rather than shortened: the names in
// here have to match, character for character, what a pass declares --
// and a shortened one matches nothing while looking like it should.
static bool Fluxion_RenderGraphAssetText_CopyName(char* dest, usize destSize, const char* source, const char* what)
{
    const usize length = strlen(source);
    if (length >= destSize)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                          "%s \"%s\" is longer than the %zu characters a name may have", what, source, destSize - 1);
        return false;
    }

    memcpy(dest, source, length);
    dest[length] = '\0';
    return true;
}

static bool Fluxion_RenderGraphAssetText_ParseImports(json_stream* json, FluxionRenderGraphAsset* asset)
{
    if (json_next(json) != JSON_ARRAY) return false;

    enum json_type type;
    while ((type = json_next(json)) == JSON_OBJECT)
    {
        if (asset->importCount >= FLUXION_RENDER_GRAPH_ASSET_MAX_IMPORTS)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "a graph may declare at most %d imports", FLUXION_RENDER_GRAPH_ASSET_MAX_IMPORTS);
            return false;
        }

        FluxionRenderGraphAssetImport* import = &asset->imports[asset->importCount];
        memset(import, 0, sizeof(*import));
        bool hasName = false;
        bool hasKind = false;

        while ((type = json_next(json)) == JSON_STRING)
        {
            // Compared where it is rather than copied out: what the
            // parser hands back stays valid until the next json_next
            // call, and every comparison below happens before one.
            const char* key = json_get_string(json, NULL);

            if (strcmp(key, "name") == 0)
            {
                if (json_next(json) != JSON_STRING) return false;
                if (!Fluxion_RenderGraphAssetText_CopyName(import->name, sizeof(import->name), json_get_string(json, NULL), "an import name")) return false;
                hasName = true;
            }
            else if (strcmp(key, "kind") == 0)
            {
                if (json_next(json) != JSON_STRING) return false;
                const char* value = json_get_string(json, NULL);
                if (strcmp(value, "texture") == 0) import->kind = FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE;
                else if (strcmp(value, "buffer") == 0) import->kind = FLUXION_RENDER_GRAPH_ASSET_IMPORT_BUFFER;
                else
                {
                    FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                                      "an import is of kind \"%s\", and a graph resource is either a texture or a buffer", value);
                    return false;
                }
                hasKind = true;
            }
            else
            {
                json_skip(json);
            }
        }
        if (type != JSON_OBJECT_END) return false;

        if (!hasName || !hasKind)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "every import needs both a name and a kind");
            return false;
        }

        // The same name twice means two bindings for one resource, and
        // nothing decides which of them wins.
        for (u32 i = 0; i < asset->importCount; ++i)
        {
            if (strcmp(asset->imports[i].name, import->name) == 0)
            {
                FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "\"%s\" is declared as an import twice", import->name);
                return false;
            }
        }

        ++asset->importCount;
    }

    return type == JSON_ARRAY_END;
}

static bool Fluxion_RenderGraphAssetText_ParseNodes(json_stream* json, FluxionRenderGraphAsset* asset)
{
    if (json_next(json) != JSON_ARRAY) return false;

    enum json_type type;
    while ((type = json_next(json)) == JSON_OBJECT)
    {
        if (asset->nodeCount >= FLUXION_RENDER_GRAPH_ASSET_MAX_NODES)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY,
                              "a graph may hold at most %d nodes", FLUXION_RENDER_GRAPH_ASSET_MAX_NODES);
            return false;
        }

        FluxionRenderGraphAssetNode* node = &asset->nodes[asset->nodeCount];
        memset(node, 0, sizeof(*node));

        while ((type = json_next(json)) == JSON_STRING)
        {
            const char* key = json_get_string(json, NULL);

            if (strcmp(key, "name") == 0)
            {
                if (json_next(json) != JSON_STRING) return false;
                if (!Fluxion_RenderGraphAssetText_CopyName(node->name, sizeof(node->name), json_get_string(json, NULL), "a node name")) return false;
            }
            else if (strcmp(key, "type") == 0)
            {
                if (json_next(json) != JSON_STRING) return false;
                if (!Fluxion_RenderGraphAssetText_CopyName(node->passType, sizeof(node->passType), json_get_string(json, NULL), "a pass type name")) return false;
            }
            else
            {
                json_skip(json);
            }
        }
        if (type != JSON_OBJECT_END) return false;

        if (node->name[0] == '\0' || node->passType[0] == '\0')
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "every node needs both a name and a type");
            return false;
        }

        // Instance names are what a dependency dump and a diagnostic
        // point at, and two nodes answering to one name make both of
        // those say nothing.
        for (u32 i = 0; i < asset->nodeCount; ++i)
        {
            if (strcmp(asset->nodes[i].name, node->name) == 0)
            {
                FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "two nodes are both called \"%s\"", node->name);
                return false;
            }
        }

        ++asset->nodeCount;
    }

    return type == JSON_ARRAY_END;
}

bool Fluxion_RenderGraphAsset_ParseText(const char* text, usize length, FluxionRenderGraphAsset* outAsset)
{
    if (text == NULL || outAsset == NULL) return false;

    // Filled in as it goes and only handed over at the end, so a file
    // that fails halfway leaves the caller's asset as it found it.
    FluxionRenderGraphAsset parsed;
    memset(&parsed, 0, sizeof(parsed));

    json_stream json;
    json_open_buffer(&json, text, length);

    bool ok = json_next(&json) == JSON_OBJECT;
    bool sawNodes = false;

    enum json_type type = JSON_ERROR;
    while (ok && (type = json_next(&json)) == JSON_STRING)
    {
        const char* key = json_get_string(&json, NULL);

        if (strcmp(key, "name") == 0)
        {
            if (json_next(&json) != JSON_STRING) { ok = false; break; }
            if (!Fluxion_RenderGraphAssetText_CopyName(parsed.name, sizeof(parsed.name), json_get_string(&json, NULL), "a graph name")) { ok = false; break; }
        }
        else if (strcmp(key, "imports") == 0)
        {
            if (!Fluxion_RenderGraphAssetText_ParseImports(&json, &parsed)) { ok = false; break; }
        }
        else if (strcmp(key, "nodes") == 0)
        {
            if (!Fluxion_RenderGraphAssetText_ParseNodes(&json, &parsed)) { ok = false; break; }
            sawNodes = true;
        }
        else
        {
            json_skip(&json);
        }
    }

    if (ok && type != JSON_OBJECT_END) ok = false;

    if (ok && !sawNodes)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_GRAPH_ASSET_LOG_CATEGORY, "a graph with no \"nodes\" is not a graph");
        ok = false;
    }

    json_close(&json);

    if (!ok) return false;

    *outAsset = parsed;
    return true;
}
