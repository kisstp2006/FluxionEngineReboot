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

// A material with an identity that survives being written down.

#include <Fluxion/RenderCore/Renderer/MaterialAsset.h>

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <string.h>

#define FLUXION_MATERIAL_ASSET_LOG_CATEGORY "MaterialAsset"

// What building a material needs, captured at registration. Static
// because the asset system holds it by pointer for as long as the type
// is registered.
typedef struct FluxionMaterialAssetContext
{
    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;
    FluxionRHIFormat colorFormat;
    FluxionRHIFormat depthFormat;
} FluxionMaterialAssetContext;

static FluxionMaterialAssetContext s_context;
static bool s_registered = false;

// The asset and its source text in one allocation: two would have to be
// tracked separately and freed in two places, either of which can be
// forgotten on its own.
typedef struct FluxionMaterialAssetBlock
{
    FluxionMaterialAsset asset;
    usize totalSize;
} FluxionMaterialAssetBlock;

FluxionAssetTypeId Fluxion_MaterialAsset_TypeId(void)
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(FLUXION_MATERIAL_ASSET_TYPE_NAME));
}

// ---------------------------------------------------------------------
// The format.
// ---------------------------------------------------------------------

bool Fluxion_MaterialAsset_Write(FluxionStream* stream, const FluxionMaterialAssetData* data)
{
    if (stream == NULL || data == NULL || !Fluxion_Stream_IsWriting(stream)) return false;
    if (data->source == NULL) return false;
    if (data->parameterCount > FLUXION_MATERIAL_ASSET_MAX_PARAMETERS) return false;
    if (data->parameterCount > 0 && data->parameters == NULL) return false;

    u32 magic = FLUXION_MATERIAL_ASSET_MAGIC;
    u32 formatVersion = FLUXION_MATERIAL_ASSET_FORMAT_VERSION;
    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    u32 sourceLength = (u32)strlen(data->source);
    Fluxion_Stream_SerializeU32(stream, &sourceLength);
    Fluxion_Stream_SerializeBytes(stream, (void*)data->source, sourceLength);

    u32 parameterCount = data->parameterCount;
    Fluxion_Stream_SerializeU32(stream, &parameterCount);
    for (u32 i = 0; i < parameterCount; ++i)
    {
        FluxionMaterialAssetParameter parameter = data->parameters[i];

        u32 nameLength = (u32)strlen(parameter.name);
        Fluxion_Stream_SerializeU32(stream, &nameLength);
        Fluxion_Stream_SerializeBytes(stream, parameter.name, nameLength);

        u32 kind = (u32)parameter.kind;
        Fluxion_Stream_SerializeU32(stream, &kind);

        Fluxion_Stream_SerializeF32(stream, &parameter.value.x);
        Fluxion_Stream_SerializeF32(stream, &parameter.value.y);
        Fluxion_Stream_SerializeF32(stream, &parameter.value.z);
        Fluxion_Stream_SerializeF32(stream, &parameter.value.w);
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_MaterialAsset_Read(const u8* bytes, usize size, FluxionMaterialAsset** outAsset)
{
    if (bytes == NULL || outAsset == NULL) return false;

    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, bytes, size);

    u32 magic = 0;
    u32 formatVersion = 0;
    Fluxion_Stream_SerializeU32(&stream, &magic);
    Fluxion_Stream_SerializeU32(&stream, &formatVersion);

    if (magic != FLUXION_MATERIAL_ASSET_MAGIC)
    {
        FLUXION_LOG_ERROR(FLUXION_MATERIAL_ASSET_LOG_CATEGORY, "these are not the bytes of a material");
        return false;
    }
    if (formatVersion > FLUXION_MATERIAL_ASSET_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_MATERIAL_ASSET_LOG_CATEGORY,
                          "this material was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    u32 sourceLength = 0;
    Fluxion_Stream_SerializeU32(&stream, &sourceLength);
    if (Fluxion_Stream_HasOverflowed(&stream)) return false;

    // Checked before anything is allocated, so a damaged file claiming
    // four gigabytes of source does not get four gigabytes asked for on
    // its own say-so.
    const usize remaining = size - Fluxion_Stream_GetPosition(&stream);
    if ((usize)sourceLength > remaining || sourceLength == 0) return false;

    const usize headerSize = sizeof(FluxionMaterialAssetBlock);
    const usize totalSize = headerSize + sourceLength + 1;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionMaterialAssetBlock* block = (FluxionMaterialAssetBlock*)Fluxion_Allocator_Alloc(allocator, totalSize, FLUXION_DEFAULT_ALIGNMENT);
    if (block == NULL) return false;

    memset(block, 0, headerSize);
    block->totalSize = totalSize;

    // EVERY HANDLE NAMED INVALID, HERE, BEFORE ANYTHING CAN FAIL.
    //
    // A zeroed handle is index zero generation zero, which is a REAL SLOT
    // and reads as valid. Loading can stop at any of a dozen points below,
    // and the release that follows destroys whatever the handles name --
    // so the moment a field exists it has to name nothing, not slot zero.
    // Measured, before this was here: a material whose load stopped early
    // destroyed the first shader program in the pool, and the fault
    // surfaced in a different test entirely.
    block->asset.program = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.prepassProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.material = (FluxionMaterialHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.pipeline = (FluxionRenderPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    char* source = (char*)block + headerSize;
    Fluxion_Stream_SerializeBytes(&stream, source, sourceLength);
    source[sourceLength] = '\0';

    u32 parameterCount = 0;
    Fluxion_Stream_SerializeU32(&stream, &parameterCount);
    if (parameterCount > FLUXION_MATERIAL_ASSET_MAX_PARAMETERS)
    {
        Fluxion_Allocator_Free(allocator, block, totalSize);
        return false;
    }

    for (u32 i = 0; i < parameterCount; ++i)
    {
        FluxionMaterialAssetParameter* parameter = &block->asset.parameters[i];

        u32 nameLength = 0;
        Fluxion_Stream_SerializeU32(&stream, &nameLength);
        if (nameLength > FLUXION_MATERIAL_ASSET_MAX_PARAMETER_NAME)
        {
            Fluxion_Allocator_Free(allocator, block, totalSize);
            return false;
        }
        Fluxion_Stream_SerializeBytes(&stream, parameter->name, nameLength);
        parameter->name[nameLength] = '\0';

        u32 kind = 0;
        Fluxion_Stream_SerializeU32(&stream, &kind);
        if (kind > FLUXION_MATERIAL_ASSET_PARAMETER_VEC4)
        {
            Fluxion_Allocator_Free(allocator, block, totalSize);
            return false;
        }
        parameter->kind = (FluxionMaterialAssetParameterKind)kind;

        Fluxion_Stream_SerializeF32(&stream, &parameter->value.x);
        Fluxion_Stream_SerializeF32(&stream, &parameter->value.y);
        Fluxion_Stream_SerializeF32(&stream, &parameter->value.z);
        Fluxion_Stream_SerializeF32(&stream, &parameter->value.w);
    }

    if (Fluxion_Stream_HasOverflowed(&stream))
    {
        Fluxion_Allocator_Free(allocator, block, totalSize);
        return false;
    }

    block->asset.parameterCount = parameterCount;
    block->asset.source = source;

    // Said outright: nothing has been given to a device yet, and slot
    // zero of either pool is a real object.
    block->asset.material = (FluxionMaterialHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.pipeline = (FluxionRenderPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.program = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    *outAsset = &block->asset;
    return true;
}

void Fluxion_MaterialAsset_Destroy(FluxionMaterialAsset* asset)
{
    if (asset == NULL) return;

    FluxionMaterialAssetBlock* block = (FluxionMaterialAssetBlock*)asset;

    // In the order they were built, backwards: the pipeline and the
    // material both name the program, so the program goes last.
    if (FLUXION_HANDLE_IS_VALID(asset->pipeline)) Fluxion_RenderPipeline_Destroy(asset->pipeline);
    if (FLUXION_HANDLE_IS_VALID(asset->material)) Fluxion_Material_Destroy(asset->material);
    if (FLUXION_HANDLE_IS_VALID(asset->program)) Fluxion_ShaderProgram_Destroy(asset->program);
    if (FLUXION_HANDLE_IS_VALID(asset->prepassProgram)) Fluxion_ShaderProgram_Destroy(asset->prepassProgram);

    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), block, block->totalSize);
}

// ---------------------------------------------------------------------
// The asset system's halves.
// ---------------------------------------------------------------------

static bool Fluxion_MaterialAsset_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    FLUXION_UNUSED(userData);

    FluxionMaterialAsset* asset = NULL;
    if (!Fluxion_MaterialAsset_Read(bytes, size, &asset)) return false;

    *outObject = asset;
    return true;
}

static bool Fluxion_MaterialAsset_Finalize(void* object, void* userData)
{
    FluxionMaterialAsset* asset = (FluxionMaterialAsset*)object;
    const FluxionMaterialAssetContext* context = (const FluxionMaterialAssetContext*)userData;

    // The two halves of one shader, built from the material's source the
    // same way a program written by hand is -- so a material that loads
    // and one that was assembled in code go through the same compiler.
    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(asset->source, FLUXION_MATERIAL_PASS_FORWARD);
    if (vertexSource == NULL || fragmentSource == NULL)
    {
        Fluxion_MaterialShader_FreeSource(vertexSource);
        Fluxion_MaterialShader_FreeSource(fragmentSource);
        return false;
    }

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    programDesc.debugName = "Fluxion.MaterialAsset.Program";

    const FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(context->device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    if (!FLUXION_HANDLE_IS_VALID(program))
    {
        FLUXION_LOG_ERROR(FLUXION_MATERIAL_ASSET_LOG_CATEGORY, "the material's own shader would not compile");
        return false;
    }

    asset->program = program;
    asset->material = Fluxion_Material_Create(context->device, program);
    if (!FLUXION_HANDLE_IS_VALID(asset->material)) return false;

    asset->pipeline = Fluxion_RenderPipeline_Create(context->device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                    context->colorFormat, context->depthFormat);
    if (!FLUXION_HANDLE_IS_VALID(asset->pipeline)) return false;

    // AND THE SAME MATERIAL AGAIN, for the pass that records the surface
    // before it is lit. The same source, a different entry point -- which
    // is what makes the recorded surface the surface that gets drawn,
    // even for a material that works its own out in an unusual way.
    //
    // NOT FATAL WHEN IT FAILS. Everything this feeds is an effect on top
    // of the picture; a material whose second program would not build
    // still loads, still draws, and is simply left out of that pass.
    char* prepassVertex = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);
    char* prepassFragment = Fluxion_MaterialShader_BuildFragmentSource(asset->source, FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);
    if (prepassVertex != NULL && prepassFragment != NULL)
    {
        FluxionShaderProgramDesc prepassDesc;
        memset(&prepassDesc, 0, sizeof(prepassDesc));
        prepassDesc.vertexSource = prepassVertex;
        prepassDesc.fragmentSource = prepassFragment;
        prepassDesc.debugName = "Fluxion.MaterialAsset.SurfaceProgram";

        asset->prepassProgram = Fluxion_ShaderProgram_Create(context->device, &prepassDesc);
        if (FLUXION_HANDLE_IS_VALID(asset->prepassProgram))
        {
            Fluxion_RenderPipeline_SetPrepassProgram(asset->pipeline, asset->prepassProgram);
        }
        else
        {
            FLUXION_LOG_WARN(FLUXION_MATERIAL_ASSET_LOG_CATEGORY,
                             "the material's surface shader would not compile; it will be left out of the pass that records surfaces");
        }
    }
    Fluxion_MaterialShader_FreeSource(prepassVertex);
    Fluxion_MaterialShader_FreeSource(prepassFragment);

    for (u32 i = 0; i < asset->parameterCount; ++i)
    {
        const FluxionMaterialAssetParameter* parameter = &asset->parameters[i];

        bool applied = false;
        switch (parameter->kind)
        {
            case FLUXION_MATERIAL_ASSET_PARAMETER_FLOAT:
                applied = Fluxion_Material_SetFloat(asset->material, parameter->name, parameter->value.x);
                break;
            case FLUXION_MATERIAL_ASSET_PARAMETER_VEC3:
            {
                const FluxionVec3 value = { parameter->value.x, parameter->value.y, parameter->value.z };
                applied = Fluxion_Material_SetVec3(asset->material, parameter->name, value);
                break;
            }
            case FLUXION_MATERIAL_ASSET_PARAMETER_VEC4:
                applied = Fluxion_Material_SetVec4(asset->material, parameter->name, parameter->value);
                break;
        }

        if (!applied)
        {
            // A value the shader has no home for is a material saying
            // something its surface cannot hear. Refused rather than
            // dropped: the alternative is a material that looks right in
            // the file and wrong on screen.
            FLUXION_LOG_ERROR(FLUXION_MATERIAL_ASSET_LOG_CATEGORY,
                              "this material sets \"%s\", which its own shader does not declare", parameter->name);
            return false;
        }
    }

    Fluxion_Material_FlushDirty(asset->material);

    // A device holds the shader now, so the copy that was read stops
    // being worth pointing at. The memory itself belongs to the one block
    // and goes when that does.
    asset->source = NULL;
    return true;
}

static void Fluxion_MaterialAsset_Unload(void* object, void* userData)
{
    FLUXION_UNUSED(userData);
    Fluxion_MaterialAsset_Destroy((FluxionMaterialAsset*)object);
}

bool Fluxion_MaterialAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                        FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat)
{
    if (s_registered) return true;

    s_context.device = device;
    s_context.queue = queue;
    s_context.colorFormat = colorFormat;
    s_context.depthFormat = depthFormat;

    FluxionAssetTypeDesc desc;
    memset(&desc, 0, sizeof(desc));

    memcpy(desc.name, FLUXION_MATERIAL_ASSET_TYPE_NAME, sizeof(FLUXION_MATERIAL_ASSET_TYPE_NAME));
    memcpy(desc.cookedExtension, "fluxmat", sizeof("fluxmat"));

    // No source extension and no import function, the same as every other
    // type this module registers: the authored form is text somebody
    // wrote, and cooking it is a call to Write.
    desc.sourceExtensionCount = 0;
    desc.import = NULL;

    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = Fluxion_MaterialAsset_Load;
    desc.finalize = Fluxion_MaterialAsset_Finalize;
    desc.unload = Fluxion_MaterialAsset_Unload;
    desc.userData = &s_context;

    s_registered = Fluxion_AssetTypes_Register(&desc);
    return s_registered;
}

void Fluxion_MaterialAsset_UnregisterType(void)
{
    if (!s_registered) return;

    Fluxion_AssetTypes_Unregister(Fluxion_MaterialAsset_TypeId());
    s_registered = false;
}
