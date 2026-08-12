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
// The format deliberately does NOT encode edges between nodes -- a pass's
// own (C/C++) Setup callback is what declares which resource *names* it
// reads/writes/creates, and Fluxion_RenderGraph_Compile derives the actual
// dependency edges from those declarations. So the JSON only ever decides
// WHICH registered pass-type instances exist and what they are named; how
// their pins wire together is inherent to each pass type's own Setup
// logic, not something an artist rewires at the JSON level.
//
// "imports" is accepted and validated for shape (each entry needs a
// "name" and a "kind" of "texture" or "buffer") but is otherwise
// informational here -- Fluxion_RenderGraph_ImportTexture/ImportBuffer
// need a real, already-created FluxionRHITextureHandle/
// FluxionRHIBufferHandle, which cannot come from a text file; a caller
// still imports the actual resource via those C calls (using the same
// resource name a JSON-described pass's Setup callback will later Read/
// Write), this section just documents which names the file's author
// expects to be supplied that way.

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
