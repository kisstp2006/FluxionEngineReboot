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

// The authored `.pipeline` reader. The format is documented alongside
// Fluxion_RenderPipelineAsset_ParseText.

#include <Fluxion/RenderCore/Pipeline/RenderPipelineAsset.h>

#include <Fluxion/Foundation/Log.h>

#include <pdjson.h>

#include <string.h>

#define FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY "RenderPipelineAsset"

// The longest a graph's authored name may be. It is looked up in a
// database and never stored, so this only has to be longer than a name
// anybody writes.
#define FLUXION_RENDER_PIPELINE_ASSET_MAX_GRAPH_NAME 128

static bool Fluxion_RenderPipelineAssetText_CopyName(char* dest, usize destSize, const char* source, const char* what)
{
    const usize length = strlen(source);
    if (length >= destSize)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "%s \"%s\" is longer than the %zu characters a name may have", what, source, destSize - 1);
        return false;
    }

    memcpy(dest, source, length);
    dest[length] = '\0';
    return true;
}

static bool Fluxion_RenderPipelineAssetText_ReadBool(json_stream* json, bool* outValue)
{
    const enum json_type type = json_next(json);
    if (type == JSON_TRUE) { *outValue = true; return true; }
    if (type == JSON_FALSE) { *outValue = false; return true; }
    return false;
}

static bool Fluxion_RenderPipelineAssetText_ReadLighting(json_stream* json, FluxionRenderPipelineLighting* outValue)
{
    if (json_next(json) != JSON_STRING) return false;

    const char* value = json_get_string(json, NULL);
    if (strcmp(value, "forward") == 0) { *outValue = FLUXION_RENDER_PIPELINE_LIGHTING_FORWARD; return true; }
    if (strcmp(value, "clustered") == 0) { *outValue = FLUXION_RENDER_PIPELINE_LIGHTING_CLUSTERED; return true; }

    FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY, "\"%s\" is not a way of lighting a frame", value);
    return false;
}

static bool Fluxion_RenderPipelineAssetText_ReadShadowQuality(json_stream* json, FluxionRenderPipelineShadowQuality* outValue)
{
    if (json_next(json) != JSON_STRING) return false;

    const char* value = json_get_string(json, NULL);
    if (strcmp(value, "off") == 0) { *outValue = FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF; return true; }
    if (strcmp(value, "low") == 0) { *outValue = FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_LOW; return true; }
    if (strcmp(value, "medium") == 0) { *outValue = FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_MEDIUM; return true; }
    if (strcmp(value, "high") == 0) { *outValue = FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH; return true; }

    FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY, "\"%s\" is not a shadow quality", value);
    return false;
}

static bool Fluxion_RenderPipelineAssetText_ReadCulling(json_stream* json, FluxionRenderPipelineCulling* outValue)
{
    if (json_next(json) != JSON_STRING) return false;

    const char* value = json_get_string(json, NULL);
    if (strcmp(value, "cpu") == 0) { *outValue = FLUXION_RENDER_PIPELINE_CULLING_CPU; return true; }
    if (strcmp(value, "gpu") == 0) { *outValue = FLUXION_RENDER_PIPELINE_CULLING_GPU; return true; }

    FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY, "\"%s\" is not somewhere culling can happen", value);
    return false;
}

bool Fluxion_RenderPipelineAsset_ParseText(const char* text, usize length, FluxionRenderPipelineGraphResolveFn resolve,
                                           void* context, FluxionRenderPipelineAsset* outAsset)
{
    if (text == NULL || resolve == NULL || outAsset == NULL) return false;

    FluxionRenderPipelineAsset parsed;
    memset(&parsed, 0, sizeof(parsed));

    // Held until the end rather than resolved where it is read: the
    // resolver is allowed to be a database lookup, and a file that turns
    // out to be malformed further down should not have caused one.
    char graphName[FLUXION_RENDER_PIPELINE_ASSET_MAX_GRAPH_NAME];
    graphName[0] = '\0';

    json_stream json;
    json_open_buffer(&json, text, length);

    bool ok = json_next(&json) == JSON_OBJECT;

    enum json_type type = JSON_ERROR;
    while (ok && (type = json_next(&json)) == JSON_STRING)
    {
        const char* key = json_get_string(&json, NULL);

        if (strcmp(key, "name") == 0)
        {
            if (json_next(&json) != JSON_STRING) { ok = false; break; }
            if (!Fluxion_RenderPipelineAssetText_CopyName(parsed.name, sizeof(parsed.name), json_get_string(&json, NULL), "a pipeline name")) { ok = false; break; }
        }
        else if (strcmp(key, "graph") == 0)
        {
            if (json_next(&json) != JSON_STRING) { ok = false; break; }
            if (!Fluxion_RenderPipelineAssetText_CopyName(graphName, sizeof(graphName), json_get_string(&json, NULL), "a graph name")) { ok = false; break; }
        }
        else if (strcmp(key, "lighting") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadLighting(&json, &parsed.settings.lighting)) { ok = false; break; }
        }
        else if (strcmp(key, "shadowQuality") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadShadowQuality(&json, &parsed.settings.shadowQuality)) { ok = false; break; }
        }
        else if (strcmp(key, "culling") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadCulling(&json, &parsed.settings.culling)) { ok = false; break; }
        }
        else if (strcmp(key, "taa") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadBool(&json, &parsed.settings.taa)) { ok = false; break; }
        }
        else if (strcmp(key, "ssao") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadBool(&json, &parsed.settings.ssao)) { ok = false; break; }
        }
        else if (strcmp(key, "ssr") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadBool(&json, &parsed.settings.ssr)) { ok = false; break; }
        }
        else if (strcmp(key, "bloom") == 0)
        {
            if (!Fluxion_RenderPipelineAssetText_ReadBool(&json, &parsed.settings.bloom)) { ok = false; break; }
        }
        else if (strcmp(key, "msaa") == 0)
        {
            if (json_next(&json) != JSON_NUMBER) { ok = false; break; }
            const double samples = json_get_number(&json);
            if (samples < 0.0 || samples > 64.0) { ok = false; break; }
            parsed.settings.msaaSamples = (u32)samples;
        }
        else
        {
            json_skip(&json);
        }
    }

    if (ok && type != JSON_OBJECT_END) ok = false;

    json_close(&json);

    if (!ok) return false;

    if (graphName[0] == '\0')
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "\"%s\" names no graph, and a pipeline that draws nothing is not a pipeline", parsed.name);
        return false;
    }

    // Before the resolve, because refusing a file for what it asks the
    // renderer to do does not need a database to be consulted first.
    const char* unsupported = NULL;
    if (!Fluxion_RenderPipelineAsset_AreSettingsSupported(&parsed.settings, &unsupported))
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "\"%s\" asks for %s, which this build has no pass for", parsed.name, unsupported);
        return false;
    }

    if (!resolve(graphName, &parsed.graph.asset, context))
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "\"%s\" is drawn by the graph \"%s\", and there is no such graph", parsed.name, graphName);
        return false;
    }

    *outAsset = parsed;
    return true;
}
