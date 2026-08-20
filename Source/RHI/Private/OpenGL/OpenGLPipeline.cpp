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

// Shader / graphics+compute pipeline (one linked GL program per pipeline),
// the raster/depth/blend state cache, and pipeline-cache save/load via
// glGetProgramBinary/glProgramBinary.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include "../PipelineCacheFile.h"

#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// --- Shaders -------------------------------------------------------------

static FluxionRHIOpenGLSlot s_shaderSlots[FLUXION_RHI_OPENGL_MAX_SHADERS];
static FluxionRHIOpenGLShader s_shaders[FLUXION_RHI_OPENGL_MAX_SHADERS];

FluxionRHIOpenGLShader* Fluxion_RHIOpenGL_ResolveShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_shaderSlots, FLUXION_RHI_OPENGL_MAX_SHADERS, shader.index, shader.generation)) return nullptr;
    return &s_shaders[shader.index];
}

static GLenum Fluxion_RHIOpenGL_MapShaderStage(FluxionRHIShaderStage stage)
{
    switch (stage)
    {
        case FLUXION_RHI_SHADER_STAGE_VERTEX: return GL_VERTEX_SHADER;
        case FLUXION_RHI_SHADER_STAGE_FRAGMENT: return GL_FRAGMENT_SHADER;
        case FLUXION_RHI_SHADER_STAGE_COMPUTE: return GL_COMPUTE_SHADER;
        default: return GL_FRAGMENT_SHADER;
    }
}

FluxionRHIShaderHandle Fluxion_RHIOpenGL_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr || desc == nullptr || desc->bytecode == nullptr) return invalid;

    // `bytecode` is GLSL source text for this backend
    // -- bytecodeSize may or may not include a null terminator, so a
    // temporary null-terminated copy is built for glShaderSource.
    std::vector<char> source(desc->bytecodeSize + 1);
    std::memcpy(source.data(), desc->bytecode, desc->bytecodeSize);
    source[desc->bytecodeSize] = '\0';

    GLenum stage = Fluxion_RHIOpenGL_MapShaderStage(desc->stage);
    GLuint shaderName = glCreateShader(stage);
    const char* sourcePtr = source.data();
    GLint sourceLength = (GLint)desc->bytecodeSize;
    glShaderSource(shaderName, 1, &sourcePtr, &sourceLength);
    glCompileShader(shaderName);

    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shaderName, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE)
    {
        GLint logLength = 0;
        glGetShaderiv(shaderName, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log((usize)(logLength > 0 ? logLength : 1));
        glGetShaderInfoLog(shaderName, (GLsizei)log.size(), nullptr, log.data());
        FLUXION_LOG_ERROR("RHI.OpenGL", "Shader compile failed (%s): %s",
            desc->debugName != nullptr ? desc->debugName : "<unnamed>", log.data());
        glDeleteShader(shaderName);
        return invalid;
    }

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_shaderSlots, FLUXION_RHI_OPENGL_MAX_SHADERS, &index, &generation))
    {
        glDeleteShader(shaderName);
        return invalid;
    }
    s_shaders[index].name = shaderName;
    s_shaders[index].stage = desc->stage;

    Fluxion_RHIOpenGL_LabelObject(GL_SHADER, shaderName, desc->debugName);

    FluxionRHIShaderHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_shaderSlots, FLUXION_RHI_OPENGL_MAX_SHADERS, shader.index, shader.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed shader handle");
        return;
    }
    if (s_shaders[shader.index].name != 0) glDeleteShader(s_shaders[shader.index].name);
    s_shaders[shader.index] = FluxionRHIOpenGLShader{};
    Fluxion_RHIOpenGL_PoolFree(s_shaderSlots, FLUXION_RHI_OPENGL_MAX_SHADERS, shader.index, shader.generation);
}

// --- Pipelines -------------------------------------------------------------

static FluxionRHIOpenGLSlot s_pipelineSlots[FLUXION_RHI_OPENGL_MAX_PIPELINES];
static FluxionRHIOpenGLPipeline s_pipelines[FLUXION_RHI_OPENGL_MAX_PIPELINES];

FluxionRHIOpenGLPipeline* Fluxion_RHIOpenGL_ResolvePipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_pipelineSlots, FLUXION_RHI_OPENGL_MAX_PIPELINES, pipeline.index, pipeline.generation)) return nullptr;
    return &s_pipelines[pipeline.index];
}

FluxionRHIOpenGLPipeline* Fluxion_RHIOpenGL_ResolvePipelineByIndex(u32 index)
{
    if (index >= FLUXION_RHI_OPENGL_MAX_PIPELINES || !s_pipelineSlots[index].alive) return nullptr;
    return &s_pipelines[index];
}

static bool Fluxion_RHIOpenGL_LinkProgram(GLuint program, const char* debugName)
{
    glLinkProgram(program);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log((usize)(logLength > 0 ? logLength : 1));
        glGetProgramInfoLog(program, (GLsizei)log.size(), nullptr, log.data());
        FLUXION_LOG_ERROR("RHI.OpenGL", "Program link failed (%s): %s", debugName != nullptr ? debugName : "<unnamed>", log.data());
        return false;
    }
    return true;
}

FluxionRHIPipelineHandle Fluxion_RHIOpenGL_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr || desc == nullptr) return invalid;

    FluxionRHIOpenGLShader* vertexShader = Fluxion_RHIOpenGL_ResolveShader(desc->vertexShader);
    FluxionRHIOpenGLShader* fragmentShader = Fluxion_RHIOpenGL_ResolveShader(desc->fragmentShader);
    if (vertexShader == nullptr || fragmentShader == nullptr) return invalid;

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader->name);
    glAttachShader(program, fragmentShader->name);
    bool linked = Fluxion_RHIOpenGL_LinkProgram(program, desc->debugName);
    glDetachShader(program, vertexShader->name);
    glDetachShader(program, fragmentShader->name);
    if (!linked)
    {
        glDeleteProgram(program);
        return invalid;
    }

    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    for (u32 i = 0; i < desc->vertexLayout.attributeCount; ++i)
    {
        const FluxionRHIVertexAttribute* attr = &desc->vertexLayout.attributes[i];
        GLenum type; GLint components; GLboolean normalized;
        Fluxion_RHIOpenGL_MapVertexAttribFormat(attr->format, &type, &components, &normalized);
        glEnableVertexArrayAttrib(vao, attr->location);
        glVertexArrayAttribFormat(vao, attr->location, components, type, normalized, attr->offset);
        glVertexArrayAttribBinding(vao, attr->location, 0); // single interleaved vertex buffer binding (slot 0), matches FluxionRHIVertexLayout's contract
    }

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_pipelineSlots, FLUXION_RHI_OPENGL_MAX_PIPELINES, &index, &generation))
    {
        glDeleteProgram(program);
        glDeleteVertexArrays(1, &vao);
        return invalid;
    }

    FluxionRHIOpenGLPipeline* pipelineState = &s_pipelines[index];
    *pipelineState = FluxionRHIOpenGLPipeline{};
    pipelineState->program = program;
    pipelineState->vao = vao;
    pipelineState->isCompute = false;

    Fluxion_RHIOpenGL_LabelObject(GL_PROGRAM, program, desc->debugName);
    pipelineState->rasterState = desc->rasterState;
    pipelineState->depthState = desc->depthState;
    pipelineState->blendState = desc->blendState;
    pipelineState->topology = desc->topology;
    pipelineState->vertexStride = desc->vertexLayout.stride;

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

FluxionRHIPipelineHandle Fluxion_RHIOpenGL_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr || desc == nullptr) return invalid;

    FluxionRHIOpenGLShader* computeShader = Fluxion_RHIOpenGL_ResolveShader(desc->computeShader);
    if (computeShader == nullptr) return invalid;

    GLuint program = glCreateProgram();
    glAttachShader(program, computeShader->name);
    bool linked = Fluxion_RHIOpenGL_LinkProgram(program, desc->debugName);
    glDetachShader(program, computeShader->name);
    if (!linked)
    {
        glDeleteProgram(program);
        return invalid;
    }

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_pipelineSlots, FLUXION_RHI_OPENGL_MAX_PIPELINES, &index, &generation))
    {
        glDeleteProgram(program);
        return invalid;
    }

    FluxionRHIOpenGLPipeline* pipelineState = &s_pipelines[index];
    *pipelineState = FluxionRHIOpenGLPipeline{};
    pipelineState->program = program;
    pipelineState->isCompute = true;

    Fluxion_RHIOpenGL_LabelObject(GL_PROGRAM, program, desc->debugName);

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_pipelineSlots, FLUXION_RHI_OPENGL_MAX_PIPELINES, pipeline.index, pipeline.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed pipeline handle");
        return;
    }
    FluxionRHIOpenGLPipeline* pipelineState = &s_pipelines[pipeline.index];
    if (pipelineState->program != 0) glDeleteProgram(pipelineState->program);
    if (pipelineState->vao != 0) glDeleteVertexArrays(1, &pipelineState->vao);
    *pipelineState = FluxionRHIOpenGLPipeline{};
    Fluxion_RHIOpenGL_PoolFree(s_pipelineSlots, FLUXION_RHI_OPENGL_MAX_PIPELINES, pipeline.index, pipeline.generation);
}

// --- State cache ------------------------------------------------------------

static GLenum Fluxion_RHIOpenGL_MapCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_NEVER: return GL_NEVER;
        case FLUXION_RHI_COMPARE_OP_LESS: return GL_LESS;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return GL_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return GL_LEQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return GL_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return GL_NOTEQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return GL_GEQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS:
        default: return GL_ALWAYS;
    }
}

void Fluxion_RHIOpenGL_ApplyPipelineState(FluxionRHIOpenGLDevice* deviceState, const FluxionRHIOpenGLPipeline* pipeline)
{
    FluxionRHIOpenGLStateCache* cache = &deviceState->stateCache;
    const FluxionRHIRasterState* raster = &pipeline->rasterState;
    const FluxionRHIDepthState* depth = &pipeline->depthState;
    const FluxionRHIBlendState* blend = &pipeline->blendState;

    if (!cache->valid || cache->cullMode != raster->cullMode)
    {
        if (raster->cullMode == FLUXION_RHI_CULL_MODE_NONE)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(raster->cullMode == FLUXION_RHI_CULL_MODE_FRONT ? GL_FRONT : GL_BACK);
        }
        cache->cullMode = raster->cullMode;
    }
    if (!cache->valid || cache->frontFaceCCW != raster->frontFaceCounterClockwise)
    {
        // Inverted, not a direct passthrough -- the same inversion the
        // D3D12 and Vulkan backends make, for the same reason: what the
        // portable flag calls the front is the opposite of what these
        // rasterizers call it, given how the geometry reaching them is
        // wound. Three backends agreeing is what matters; the flag's own
        // sense is the thing that is off, and correcting it belongs where
        // the flag is set, not here.
        glFrontFace(raster->frontFaceCounterClockwise ? GL_CW : GL_CCW);
        cache->frontFaceCCW = raster->frontFaceCounterClockwise;
    }
    if (!cache->valid || cache->wireframe != raster->wireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, raster->wireframe ? GL_LINE : GL_FILL);
        cache->wireframe = raster->wireframe;
    }
    if (!cache->valid || cache->depthTestEnable != depth->testEnable)
    {
        if (depth->testEnable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        cache->depthTestEnable = depth->testEnable;
    }
    if (!cache->valid || cache->depthWriteEnable != depth->writeEnable)
    {
        glDepthMask(depth->writeEnable ? GL_TRUE : GL_FALSE);
        cache->depthWriteEnable = depth->writeEnable;
    }
    if (!cache->valid || cache->depthCompareOp != depth->compareOp)
    {
        glDepthFunc(Fluxion_RHIOpenGL_MapCompareOp(depth->compareOp));
        cache->depthCompareOp = depth->compareOp;
    }
    if (!cache->valid || cache->blendMode != blend->mode)
    {
        if (blend->mode == FLUXION_RHI_BLEND_MODE_NONE)
        {
            glDisable(GL_BLEND);
        }
        else
        {
            glEnable(GL_BLEND);
            if (blend->mode == FLUXION_RHI_BLEND_MODE_ADD) glBlendFunc(GL_ONE, GL_ONE);
            else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        cache->blendMode = blend->mode;
    }
    cache->valid = true;
}

// --- Pipeline cache ---------------------------------------------------------
//
// Payload: [u32 programCount], then per program [u32 binaryFormat]
// [u32 blobSize][bytes], wrapped in the shared engine header. Loading
// only primes the driver's own cache, via glProgramBinary on scratch
// objects immediately deleted -- the RHI contract has no stable key to
// attach a binary to a future pipeline. The identity check up front is
// what stops a foreign file being fed through that path slowly and
// silently.

// GL exposes no adapter ids, only strings, so the strings are what
// identifies the device here.
static FluxionRHIPipelineCacheIdentity Fluxion_RHIOpenGL_PipelineCacheIdentity()
{
    FluxionRHIPipelineCacheIdentity identity = {};
    identity.backend = FLUXION_RHI_BACKEND_OPENGL;

    // Joined with a separator and hashed once, rather than combining
    // three hashes by hand: a separator keeps two different splits of the
    // same characters from colliding, and there is no second hash
    // construction here to get subtly wrong.
    std::string description;
    const GLenum names[] = { GL_VENDOR, GL_RENDERER, GL_VERSION };
    for (GLenum name : names)
    {
        const char* text = (const char*)glGetString(name);
        description += text != nullptr ? text : "";
        description += '\n';
    }
    identity.extra = Fluxion_HashBytes64(description.data(), description.size());
    return identity;
}

bool Fluxion_RHIOpenGL_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr || !deviceState->hasProgramBinary) return false;

    std::vector<GLuint> programs;
    for (u32 i = 0; i < FLUXION_RHI_OPENGL_MAX_PIPELINES; ++i)
    {
        if (s_pipelineSlots[i].alive && s_pipelines[i].program != 0) programs.push_back(s_pipelines[i].program);
    }
    if (programs.empty()) return false;

    std::vector<u8> payload;
    const auto appendU32 = [&payload](u32 value)
    {
        payload.push_back((u8)(value & 0xFFu));
        payload.push_back((u8)((value >> 8) & 0xFFu));
        payload.push_back((u8)((value >> 16) & 0xFFu));
        payload.push_back((u8)((value >> 24) & 0xFFu));
    };

    appendU32((u32)programs.size());
    for (GLuint program : programs)
    {
        GLint binaryLength = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
        std::vector<u8> blob((usize)(binaryLength > 0 ? binaryLength : 0));
        GLenum binaryFormat = 0;
        GLsizei written = 0;
        if (!blob.empty()) glGetProgramBinary(program, (GLsizei)blob.size(), &written, &binaryFormat, blob.data());

        appendU32((u32)binaryFormat);
        appendU32((u32)written);
        payload.insert(payload.end(), blob.begin(), blob.begin() + (written > 0 ? written : 0));
    }

    return Fluxion_RHIPipelineCacheFile_Write(path, Fluxion_RHIOpenGL_PipelineCacheIdentity(), payload.data(), payload.size());
}

bool Fluxion_RHIOpenGL_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr || !deviceState->hasProgramBinary) return false;

    std::vector<u8> payload;
    if (!Fluxion_RHIPipelineCacheFile_Read(path, Fluxion_RHIOpenGL_PipelineCacheIdentity(), &payload)) return false;

    // Every read is bounded by what is actually left, and a short read
    // stops the walk rather than trusting a count the file supplied. The
    // hash check above means damage is normally caught before reaching
    // here; these checks are what keep a file that hashes correctly but
    // frames incorrectly from walking off the end.
    usize cursor = 0;
    const auto takeU32 = [&payload, &cursor](u32* out) -> bool
    {
        if (payload.size() - cursor < 4) return false;
        *out = (u32)payload[cursor] | ((u32)payload[cursor + 1] << 8) | ((u32)payload[cursor + 2] << 16) | ((u32)payload[cursor + 3] << 24);
        cursor += 4;
        return true;
    };

    u32 count = 0;
    if (!takeU32(&count)) return false;

    bool anyLoaded = false;
    for (u32 i = 0; i < count; ++i)
    {
        u32 formatValue = 0, blobSize = 0;
        if (!takeU32(&formatValue) || !takeU32(&blobSize)) return anyLoaded;
        if (payload.size() - cursor < blobSize) return anyLoaded;

        const u8* blob = payload.data() + cursor;
        cursor += blobSize;
        if (blobSize == 0) continue;

        // Prime the driver's own program-binary cache: load into a
        // scratch program object, then discard it -- this only helps a
        // driver that keys its shader cache off the binary content
        // itself (most desktop drivers do).
        GLuint scratchProgram = glCreateProgram();
        glProgramBinary(scratchProgram, (GLenum)formatValue, blob, (GLsizei)blobSize);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(scratchProgram, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_TRUE) anyLoaded = true;
        glDeleteProgram(scratchProgram);
    }

    return anyLoaded;
}
