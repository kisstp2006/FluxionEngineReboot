// Shader / graphics+compute pipeline (one linked GL program per pipeline,
// design decision #4), the raster/depth/blend state cache (design decision
// #5), and pipeline-cache save/load via glGetProgramBinary/glProgramBinary
// (design decision #4's "minimal, correct" custom file format).

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include <Fluxion/Foundation/Log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

    // `bytecode` is GLSL source text for this backend (design decision #4)
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

// --- State cache (design decision #5) ---------------------------------------

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
        glFrontFace(raster->frontFaceCounterClockwise ? GL_CCW : GL_CW);
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
    if (!cache->valid || cache->blendEnable != blend->blendEnable)
    {
        if (blend->blendEnable)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
        cache->blendEnable = blend->blendEnable;
    }
    cache->valid = true;
}

// --- Pipeline cache (design decision #4) ------------------------------------
//
// A minimal custom file format: [u32 magic][u32 programCount] then, per
// program, [u32 nameHashPlaceholder][GLenum binaryFormat][u32 blobSize]
// [blob bytes]. Loading only ever primes the driver's own internal shader
// cache as a side effect of calling glProgramBinary on scratch program
// objects that are immediately deleted -- there is no way to retroactively
// attach a loaded binary to a *future* Fluxion_RHI_CreateGraphicsPipeline
// call without a stable cache key the RHI contract doesn't provide, so
// this follows the same "prime the driver cache, let it do the real work"
// approach most OpenGL engines use for GL_ARB_get_program_binary.

static constexpr u32 FLUXION_RHIOPENGL_PIPELINE_CACHE_MAGIC = 0x464C5847u; // "FLXG"

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

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "wb");
    if (file == nullptr) return false;
#endif

    u32 magic = FLUXION_RHIOPENGL_PIPELINE_CACHE_MAGIC;
    u32 count = (u32)programs.size();
    fwrite(&magic, sizeof(magic), 1, file);
    fwrite(&count, sizeof(count), 1, file);

    for (GLuint program : programs)
    {
        GLint binaryLength = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
        std::vector<u8> blob((usize)(binaryLength > 0 ? binaryLength : 0));
        GLenum binaryFormat = 0;
        GLsizei written = 0;
        if (!blob.empty()) glGetProgramBinary(program, (GLsizei)blob.size(), &written, &binaryFormat, blob.data());

        u32 formatValue = (u32)binaryFormat;
        u32 blobSize = (u32)written;
        fwrite(&formatValue, sizeof(formatValue), 1, file);
        fwrite(&blobSize, sizeof(blobSize), 1, file);
        if (blobSize > 0) fwrite(blob.data(), 1, blobSize, file);
    }

    fclose(file);
    return true;
}

bool Fluxion_RHIOpenGL_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr || !deviceState->hasProgramBinary) return false;

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "rb");
    if (file == nullptr) return false;
#endif

    u32 magic = 0, count = 0;
    if (fread(&magic, sizeof(magic), 1, file) != 1 || magic != FLUXION_RHIOPENGL_PIPELINE_CACHE_MAGIC ||
        fread(&count, sizeof(count), 1, file) != 1)
    {
        fclose(file);
        return false;
    }

    bool anyLoaded = false;
    for (u32 i = 0; i < count; ++i)
    {
        u32 formatValue = 0, blobSize = 0;
        if (fread(&formatValue, sizeof(formatValue), 1, file) != 1 || fread(&blobSize, sizeof(blobSize), 1, file) != 1)
        {
            fclose(file);
            return anyLoaded;
        }
        std::vector<u8> blob((usize)blobSize);
        if (blobSize > 0 && fread(blob.data(), 1, blobSize, file) != blobSize)
        {
            fclose(file);
            return anyLoaded;
        }
        if (blobSize == 0) continue;

        // Prime the driver's own program-binary cache: load into a
        // scratch program object, then discard it -- this only helps a
        // driver that keys its shader cache off the binary content
        // itself (most desktop drivers do), matching the comment above.
        GLuint scratchProgram = glCreateProgram();
        glProgramBinary(scratchProgram, (GLenum)formatValue, blob.data(), (GLsizei)blobSize);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(scratchProgram, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_TRUE) anyLoaded = true;
        glDeleteProgram(scratchProgram);
    }

    fclose(file);
    return anyLoaded;
}
