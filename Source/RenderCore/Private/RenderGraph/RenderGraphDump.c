#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>

bool Fluxion_RenderGraph_DumpDot(FluxionRenderGraph* graph, FILE* file)
{
    FLUXION_ASSERT(graph != NULL && file != NULL);

    if (!graph->compiled) return false;

    fprintf(file, "digraph RenderGraph {\n");

    for (u32 i = 0; i < graph->nodeCount; ++i)
    {
        const FluxionRenderGraphNode* node = &graph->nodes[i];
        fprintf(file, "    N%u [label=\"%s\"%s];\n", i, node->name, node->culled ? ", style=dashed" : "");
    }

    for (u32 i = 0; i < graph->edgeCount; ++i)
    {
        fprintf(file, "    N%u -> N%u;\n", graph->edgeFrom[i], graph->edgeTo[i]);
    }

    fprintf(file, "}\n");
    return true;
}
