#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-capacity pool, same convention as every other fixed-size registry
// in this codebase (e.g. FLUXION_MAX_SERVICES) -- 64 is far more than any
// single application is expected to register (one entry per distinct pass
// *type*, not per node instance; a graph can instantiate the same type any
// number of times), revisit if that assumption stops holding.
#define FLUXION_RENDER_GRAPH_MAX_PASS_TYPES 64

void Fluxion_RenderGraphPassRegistry_Init(void);
void Fluxion_RenderGraphPassRegistry_Shutdown(void);

// Copies *passType by value into the registry (the caller's struct does
// not need to outlive this call) -- false if the registry is full or
// passType->name is already registered.
bool Fluxion_RenderGraphPassRegistry_Register(const FluxionRenderGraphPassType* passType);
void Fluxion_RenderGraphPassRegistry_Unregister(const char* name);

// NULL if no pass type is registered under this name.
const FluxionRenderGraphPassType* Fluxion_RenderGraphPassRegistry_Find(const char* name);

#ifdef __cplusplus
}
#endif
