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

// The same file for every pass today -- see the header for why that is a
// statement rather than an oversight.
static const char* const s_vertexPassIncludes[FLUXION_MATERIAL_PASS_COUNT] = {
    "#include \"Fluxion/Pass/Vertex.jsl\"\n",
    "#include \"Fluxion/Pass/Vertex.jsl\"\n",
};

const char* Fluxion_MaterialShader_GetPassInclude(FluxionMaterialPass pass)
{
    if ((u32)pass >= (u32)FLUXION_MATERIAL_PASS_COUNT) return NULL;
    return s_passIncludes[pass];
}

const char* Fluxion_MaterialShader_GetVertexPassInclude(FluxionMaterialPass pass)
{
    if ((u32)pass >= (u32)FLUXION_MATERIAL_PASS_COUNT) return NULL;
    return s_vertexPassIncludes[pass];
}

char* Fluxion_MaterialShader_BuildVertexSource(FluxionMaterialPass pass)
{
    const char* include = Fluxion_MaterialShader_GetVertexPassInclude(pass);
    if (include == NULL) return NULL;

    const usize length = strlen(include);
    char* source = (char*)Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), length + 1, FLUXION_DEFAULT_ALIGNMENT);
    if (source == NULL) return NULL;

    memcpy(source, include, length);
    source[length] = '\0';
    return source;
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
