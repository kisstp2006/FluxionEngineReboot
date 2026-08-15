#include <Fluxion/RenderCore/Renderer/MaterialShader.h>

#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

// One include line per pass, and the order matters: it is appended AFTER
// the material's own source, because the entry point inside it calls the
// function the material declares.
static const char* const s_passIncludes[FLUXION_MATERIAL_PASS_COUNT] = {
    "#include \"Fluxion/Pass/Forward.jsl\"\n",
    "#include \"Fluxion/Pass/DepthOnly.jsl\"\n",
};

const char* Fluxion_MaterialShader_GetPassInclude(FluxionMaterialPass pass)
{
    if ((u32)pass >= (u32)FLUXION_MATERIAL_PASS_COUNT) return NULL;
    return s_passIncludes[pass];
}

char* Fluxion_MaterialShader_BuildFragmentSource(const char* materialSource, FluxionMaterialPass pass)
{
    const char* tail = Fluxion_MaterialShader_GetPassInclude(pass);
    if (materialSource == NULL || tail == NULL) return NULL;

    const usize sourceLength = strlen(materialSource);
    const usize tailLength = strlen(tail);

    // A newline between the two, always. A material whose last line has
    // no terminator would otherwise run into the include directive, and a
    // directive that does not begin its own line is not a directive --
    // which fails as a parse error somewhere in the material, about
    // something the author did not write.
    const usize total = sourceLength + 1 + tailLength + 1;

    char* combined = (char*)Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), total, FLUXION_DEFAULT_ALIGNMENT);
    if (combined == NULL) return NULL;

    memcpy(combined, materialSource, sourceLength);
    combined[sourceLength] = '\n';
    memcpy(combined + sourceLength + 1, tail, tailLength);
    combined[sourceLength + 1 + tailLength] = '\0';

    return combined;
}

void Fluxion_MaterialShader_FreeSource(char* source)
{
    if (source == NULL) return;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), source, strlen(source) + 1);
}
