#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/RenderCore/Renderer/TextureAsset.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/TextureCompress/BlockCompress.h>

#include <cstring>
#include <vector>

// Asking the hardware what it makes of a block this engine wrote.
//
// Everything else about the encoders can be checked on a CPU: the size
// arithmetic, the round trip, even a block worked out by hand. What none
// of it can settle is whether the bits are where the format says, because
// an encoder and a decoder written from the same misreading of a
// specification agree with each other perfectly and are both wrong.
//
// So this draws the compressed texture through a real device -- one texel
// of it per pixel, nearest sampling, no filtering to blur the answer --
// reads the result back, and compares it against what this engine's own
// decoder says the same blocks mean. The fixed-function decoder in the
// driver is the authority; the CPU decoder is the thing on trial.

namespace
{

constexpr u32 kSize = 16; // four blocks by four

struct GpuFixture
{
    FluxionRHIInstanceHandle instance{};
    FluxionRHIDeviceHandle device{};
    FluxionRHIQueueHandle queue{};
    bool usable = false;
};

GpuFixture CreateGpuFixture(FluxionRHIBackendType backend, const char* backendName)
{
    GpuFixture fixture;

    if (!Fluxion_RHI_IsBackendAvailable(backend))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s is not in this build -- skipping its block-decode comparison.", backendName);
        return fixture;
    }

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", true };
    fixture.instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(fixture.instance))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No usable %s loader -- skipping its block-decode comparison.", backendName);
        return fixture;
    }

    FluxionRHIAdapterHandle adapters[8];
    if (Fluxion_RHI_EnumerateAdapters(fixture.instance, adapters, 8) == 0)
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No %s adapters -- skipping its block-decode comparison.", backendName);
        Fluxion_RHI_DestroyInstance(fixture.instance);
        fixture.instance = FluxionRHIInstanceHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        return fixture;
    }

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(fixture.device))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s device creation failed -- skipping its block-decode comparison.", backendName);
        Fluxion_RHI_DestroyInstance(fixture.instance);
        fixture.instance = FluxionRHIInstanceHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        return fixture;
    }

    fixture.queue = Fluxion_RHI_GetQueue(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    fixture.usable = FLUXION_HANDLE_IS_VALID(fixture.queue);
    return fixture;
}

void DestroyGpuFixture(GpuFixture& fixture)
{
    if (FLUXION_HANDLE_IS_VALID(fixture.device)) Fluxion_RHI_DestroyDevice(fixture.device);
    if (FLUXION_HANDLE_IS_VALID(fixture.instance)) Fluxion_RHI_DestroyInstance(fixture.instance);
}

// Deliberately not a smooth gradient. Sharp edges that land ON block
// boundaries and sharp edges that land INSIDE one are different problems
// for a block format, and an image made only of the first kind would let
// a wrong index layout pass.
std::vector<u8> MakeSourceImage()
{
    std::vector<u8> image(kSize * kSize * 4);

    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            u8* texel = image.data() + ((usize)y * kSize + x) * 4;
            const bool blockCheck = (((x / 4) + (y / 4)) & 1u) != 0;

            texel[0] = (u8)(blockCheck ? 20u + x * 14u : 240u - x * 14u);
            texel[1] = (u8)(y * 16u);
            texel[2] = (u8)(blockCheck ? 200u : 40u);
            texel[3] = 255;
        }
    }

    return image;
}

// The same picture with values above one in it, which is the only thing
// the high-dynamic-range format is for. The range within any one block
// stays modest -- a real block covers sixteen adjacent texels, not a
// whole scene -- while the range across the image is wide.
std::vector<f32> MakeSourceImageHdr()
{
    std::vector<f32> image(kSize * kSize * 4);

    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            f32* texel = image.data() + ((usize)y * kSize + x) * 4;
            const bool blockCheck = (((x / 4) + (y / 4)) & 1u) != 0;

            texel[0] = (blockCheck ? 0.5f : 40.0f) + (f32)x * 0.25f;
            texel[1] = 0.125f + (f32)y * 1.5f;
            texel[2] = blockCheck ? 12.0f : 0.75f;
            texel[3] = 1.0f;
        }
    }

    return image;
}

// Half precision, one direction only: the expectation comes out of the
// decoder as floats and the target holds halves, so the comparison has to
// happen in one space or the other, and halves are the space the hardware
// actually produced.
u16 FloatToHalfBits(f32 value)
{
    if (!(value > 0.0f)) return 0;
    if (value > 65504.0f) value = 65504.0f;

    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));

    const i32 exponent = (i32)((bits >> 23) & 0xFFu) - 127;
    const u32 mantissa = bits & 0x7FFFFFu;

    if (exponent < -24) return 0;
    if (exponent < -14)
    {
        const u32 shift = (u32)(-exponent - 14);
        const u32 full = mantissa | 0x800000u;
        return (u16)((full + (1u << (12u + shift))) >> (13u + shift));
    }

    u32 half = ((u32)(exponent + 15) << 10) | (mantissa >> 13);
    if ((mantissa & 0x1000u) != 0) half += 1u;
    return (u16)half;
}

// The quad, in clip space, drawn with both windings.
//
// Two windings rather than one because which way round a front face is
// belongs to the pipeline's raster state, and this test is not about
// that. Whichever one survives culling covers the target; the other
// contributes nothing, since both sit at the same depth and the second
// cannot pass a strictly-nearer test.
struct QuadVertex
{
    f32 position[3];
    f32 uv[2];
};

const QuadVertex kQuadVertices[4] = {
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
    { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
    { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
    { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
};

const u16 kQuadIndices[12] = {
    0, 1, 2, 0, 2, 3, // one way round
    0, 2, 1, 0, 3, 2, // and the other
};

const char* kVertexSource =
    "[Input] Vector3 position;\n"
    "[Input] Vector2 uv;\n"
    "[Output] Vector2 vUV;\n"
    "[Output] Vector4 Position;\n"
    "[Uniform(Frame)] Matrix4x4 viewProjection;\n"
    "[Uniform(Object)] Matrix4x4 model;\n"
    "void main() {\n"
    "  vUV = uv;\n"
    "  Position = viewProjection * model * Vector4(position, 1.0);\n"
    "}\n";

// Nothing but the sample. No tint, no lighting, no curve -- anything else
// would sit between the decoder and the answer.
const char* kFragmentSource =
    "[Input] Vector2 vUV;\n"
    "[Texture(Material)] Texture2D source;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return texture(source, vUV);\n"
    "}\n";

// Uploads one compressed level through the same staging layout the
// texture asset uses -- not a second implementation of it, because a
// second one could agree with the driver while the real one did not.
bool UploadCompressedLevel(const GpuFixture& fixture, FluxionRHITextureHandle texture, FluxionRHIFormat format,
                           const std::vector<u8>& blocks)
{
    FluxionTextureLevelPlacement placement{};
    u32 placementCount = 0;
    const usize stagingSize = Fluxion_TextureAsset_PlanUpload(format, kSize, kSize, 1, 1, &placement, 1, &placementCount);
    if (stagingSize == 0 || placementCount != 1) return false;

    FluxionRHIBufferDesc stagingDesc{};
    stagingDesc.size = stagingSize;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "BlockCompressGPU.Staging";
    FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(fixture.device, &stagingDesc);
    if (!FLUXION_HANDLE_IS_VALID(staging)) return false;

    {
        u8* mapped = (u8*)Fluxion_RHI_MapBuffer(staging);
        if (mapped == nullptr)
        {
            Fluxion_RHI_DestroyBuffer(staging);
            return false;
        }

        std::memset(mapped, 0, stagingSize);
        for (u32 row = 0; row < placement.rows; ++row)
        {
            std::memcpy(mapped + placement.stagingOffset + (usize)row * placement.stagingRowBytes,
                        blocks.data() + (usize)row * placement.sourceRowBytes,
                        placement.sourceRowBytes);
        }
        Fluxion_RHI_UnmapBuffer(staging);
    }

    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(commandList);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier toCopy = { texture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);
    Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, staging, placement.stagingOffset, texture, 0, 0);
    FluxionRHIBarrier toRead = { texture, noBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
    Fluxion_RHI_CommandList_End(commandList);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(fixture.device, false);
    Fluxion_RHI_Queue_Submit(fixture.queue, &commandList, 1, fence);
    const bool completed = Fluxion_RHI_WaitForFence(fence);

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyBuffer(staging);
    Fluxion_RHI_Device_CollectGarbage(fixture.device);

    return completed;
}

// How far apart two images are, at their worst single channel. A vertical
// flip is compared separately by the caller: which way up a clip-space
// quad lands is the engine's own convention and is exercised by the
// sample that draws one, not by this.
int WorstChannelDifference(const std::vector<u8>& a, const std::vector<u8>& b, bool flipB)
{
    int worst = 0;

    for (u32 y = 0; y < kSize; ++y)
    {
        const u32 sourceY = flipB ? (kSize - 1u - y) : y;
        for (u32 x = 0; x < kSize; ++x)
        {
            for (u32 channel = 0; channel < 3; ++channel) // alpha is not written by the target's blend state
            {
                const int left = (int)a[((usize)y * kSize + x) * 4 + channel];
                const int right = (int)b[((usize)sourceY * kSize + x) * 4 + channel];
                int difference = left - right;
                if (difference < 0) difference = -difference;
                if (difference > worst) worst = difference;
            }
        }
    }

    return worst;
}

// The same, counted in half-precision steps rather than in eight-bit ones.
// A difference of one here means one representable value apart, which is
// the finest thing that can be said about a floating-point result.
int WorstChannelDifferenceHalf(const std::vector<u16>& a, const std::vector<u16>& b, bool flipB)
{
    int worst = 0;

    for (u32 y = 0; y < kSize; ++y)
    {
        const u32 sourceY = flipB ? (kSize - 1u - y) : y;
        for (u32 x = 0; x < kSize; ++x)
        {
            for (u32 channel = 0; channel < 3; ++channel)
            {
                const int left = (int)a[((usize)y * kSize + x) * 4 + channel];
                const int right = (int)b[((usize)sourceY * kSize + x) * 4 + channel];
                int difference = left - right;
                if (difference < 0) difference = -difference;
                if (difference > worst) worst = difference;
            }
        }
    }

    return worst;
}

void CompareOneFormatAgainstTheHardware(TestContext* ctx, const GpuFixture& fixture, FluxionRHIFormat format,
                                        const char* formatName, const char* backendName, int allowedDifference)
{
    // Which pixels a format reads is the format's own answer, not a
    // second table here: BC6H works in floats because a format for values
    // above one cannot be handed bytes.
    const bool isHdr = Fluxion_TextureCompress_GetPixelLayout(format) == FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA32F;

    const std::vector<u8> source = MakeSourceImage();
    const std::vector<f32> sourceHdr = MakeSourceImageHdr();

    std::vector<u8> blocks(Fluxion_TextureCompress_GetOutputSize(format, kSize, kSize));
    TEST_CHECK(ctx, !blocks.empty());
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(format, isHdr ? (const void*)sourceHdr.data() : (const void*)source.data(),
                                                   kSize, kSize, blocks.data(), blocks.size()));

    // What this engine believes those blocks mean.
    std::vector<u8> expected(kSize * kSize * 4);
    std::vector<f32> expectedHdr(kSize * kSize * 4);
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(format, blocks.data(), blocks.size(), kSize, kSize,
                                                   isHdr ? (void*)expectedHdr.data() : (void*)expected.data()));

    std::vector<u16> expectedHalf(kSize * kSize * 4);
    if (isHdr)
    {
        for (usize i = 0; i < expectedHdr.size(); ++i) expectedHalf[i] = FloatToHalfBits(expectedHdr[i]);
    }

    FluxionRHITextureDesc compressedDesc{};
    compressedDesc.width = kSize;
    compressedDesc.height = kSize;
    compressedDesc.depth = 1;
    compressedDesc.mipLevels = 1;
    compressedDesc.arrayLayers = 1;
    compressedDesc.sampleCount = 1;
    compressedDesc.format = format;
    compressedDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    compressedDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    compressedDesc.debugName = "BlockCompressGPU.Compressed";

    // Asked before creating, not after. A backend told to make a texture
    // in a format the hardware does not have reports an error, and a
    // build with validation on stops on a reported error -- so a check
    // that discovered the answer by trying would take the whole run down
    // on any machine without the format.
    if (!Fluxion_RHI_Device_IsFormatSupported(fixture.device, format))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s: this adapter has no %s -- that encoder is NOT checked here.", backendName, formatName);
        return;
    }

    FluxionRHITextureHandle compressed = Fluxion_RHI_CreateTexture(fixture.device, &compressedDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(compressed));
    if (!FLUXION_HANDLE_IS_VALID(compressed)) return;

    if (!UploadCompressedLevel(fixture, compressed, format, blocks))
    {
        TEST_CHECK(ctx, false);
        Fluxion_RHI_DestroyTexture(compressed);
        return;
    }

    FluxionRHITextureViewDesc compressedViewDesc = { compressed, format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle compressedView = Fluxion_RHI_CreateTextureView(fixture.device, &compressedViewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(compressedView));

    // Nearest, and clamped. Any filtering at all would mix neighbouring
    // texels and turn a wrong index into a slightly-off colour instead of
    // an obviously wrong one.
    FluxionRHISamplerDesc samplerDesc{};
    samplerDesc.minFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "BlockCompressGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(fixture.device, &samplerDesc);

    FluxionRHITextureDesc colorDesc = compressedDesc;
    // An eight-bit target would clamp everything above one back to one,
    // which is precisely what this format exists to avoid -- the check
    // would then pass for an encoder that threw the bright end away.
    colorDesc.format = isHdr ? FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT : FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    colorDesc.debugName = "BlockCompressGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(fixture.device, &colorDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(color));

    FluxionRHITextureViewDesc colorViewDesc = { color, colorDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(fixture.device, &colorViewDesc);

    FluxionRHITextureDesc depthDesc = compressedDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "BlockCompressGPU.Depth";
    FluxionRHITextureHandle depth = Fluxion_RHI_CreateTexture(fixture.device, &depthDesc);
    FluxionRHITextureViewDesc depthViewDesc = { depth, depthDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(fixture.device, &depthViewDesc);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "BlockCompressGPU.Program";
    programDesc.vertexSource = kVertexSource;
    programDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(fixture.device, &programDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

    if (FLUXION_HANDLE_IS_VALID(program))
    {
        FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));
        TEST_CHECK(ctx, Fluxion_Material_SetTexture(material, "source", compressedView, sampler));
        Fluxion_Material_FlushDirty(material);

        FluxionMeshBufferDesc meshDesc{};
        meshDesc.vertexData = kQuadVertices;
        meshDesc.vertexDataSize = sizeof(kQuadVertices);
        meshDesc.indexData = kQuadIndices;
        meshDesc.indexDataSize = sizeof(kQuadIndices);
        meshDesc.use16BitIndices = true;
        meshDesc.vertexLayout.attributes[0].location = 0;
        meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        meshDesc.vertexLayout.attributes[0].offset = offsetof(QuadVertex, position);
        meshDesc.vertexLayout.attributes[1].location = 1;
        meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
        meshDesc.vertexLayout.attributes[1].offset = offsetof(QuadVertex, uv);
        meshDesc.vertexLayout.attributeCount = 2;
        meshDesc.vertexLayout.stride = sizeof(QuadVertex);
        meshDesc.bounds = FluxionAABB{ FluxionVec3{ -1.0f, -1.0f, 0.0f }, FluxionVec3{ 1.0f, 1.0f, 0.0f } };
        meshDesc.debugName = "BlockCompressGPU.Quad";
        FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(fixture.device, fixture.queue, &meshDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

        FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(fixture.device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                                            colorDesc.format, depthDesc.format);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pipeline));

        FluxionRendererHandle renderer = Fluxion_Renderer_Create(fixture.device, fixture.queue);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

        FluxionRenderTargetDesc targetDesc{};
        targetDesc.colorViews[0] = colorView;
        targetDesc.colorViewCount = 1;
        targetDesc.depthView = depthView;
        FluxionRenderTargetHandle target = Fluxion_RenderTarget_Create(fixture.device, &targetDesc);

        // Identity both ways, so the quad's clip-space corners arrive
        // unchanged: a projection here would only be a second place for
        // the mapping from texel to pixel to go wrong.
        FluxionRenderViewDesc viewDesc{};
        viewDesc.viewMatrix = Fluxion_Mat4_Identity();
        viewDesc.projectionMatrix = Fluxion_Mat4_Identity();
        viewDesc.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kSize, (f32)kSize, 0.0f, 1.0f };
        viewDesc.scissor = FluxionScissorRect{ 0, 0, kSize, kSize };
        viewDesc.renderTarget = target;
        viewDesc.layerMask = 0xFFFFFFFFu;
        FluxionRenderViewHandle view = Fluxion_RenderView_Create(fixture.device, &viewDesc);
        Fluxion_RenderView_UpdateFrameConstants(view);

        const usize renderedRowBytes = Fluxion_RHI_GetFormatRowBytes(colorDesc.format, kSize);
        const usize readbackRowBytes = (renderedRowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
        FluxionRHIBufferDesc readbackDesc{};
        readbackDesc.size = readbackRowBytes * kSize;
        readbackDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
        readbackDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_READBACK;
        readbackDesc.debugName = "BlockCompressGPU.Readback";
        FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(fixture.device, &readbackDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(readback));

        FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
        Fluxion_RHI_CommandList_Begin(cmd);

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", color, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Depth", depth, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        Fluxion_Renderer_BeginFrame(renderer, view);
        Fluxion_Renderer_DrawMesh(renderer, mesh, material, pipeline, nullptr);
        TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));
        Fluxion_RenderGraph_Execute(graph, cmd);
        Fluxion_Renderer_EndFrame(renderer, cmd);

        FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRHIBarrier toSource = { color, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE };
        Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
        Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, color, 0, 0, readback, 0);
        Fluxion_RHI_CommandList_End(cmd);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(fixture.device, false);
        Fluxion_RHI_Queue_Submit(fixture.queue, &cmd, 1, fence);
        TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

        std::vector<u8> rendered(kSize * kSize * 4, 0);
        std::vector<u16> renderedHalf(kSize * kSize * 4, 0);
        {
            const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(readback);
            TEST_CHECK(ctx, mapped != nullptr);
            if (mapped != nullptr)
            {
                for (u32 y = 0; y < kSize; ++y)
                {
                    void* destination = isHdr ? (void*)(renderedHalf.data() + (usize)y * kSize * 4)
                                              : (void*)(rendered.data() + (usize)y * kSize * 4);
                    std::memcpy(destination, mapped + (usize)y * readbackRowBytes, renderedRowBytes);
                }
                Fluxion_RHI_UnmapBuffer(readback);
            }
        }

        const int straight = isHdr ? WorstChannelDifferenceHalf(renderedHalf, expectedHalf, false)
                                   : WorstChannelDifference(rendered, expected, false);
        const int flipped = isHdr ? WorstChannelDifferenceHalf(renderedHalf, expectedHalf, true)
                                  : WorstChannelDifference(rendered, expected, true);
        const int worst = straight < flipped ? straight : flipped;

        // How close is close enough is not the same question for every
        // format, and the difference is in the specifications rather than
        // in this test. BC7 and BC6H are defined to decode EXACTLY -- the
        // arithmetic is integer and every implementation must produce the
        // same number -- so nothing but zero will do, and zero is what
        // makes this a real check on the bit layout. BC4 and BC5 are
        // defined with their interpolants as exact fractions and a
        // tolerance for whoever implements them, so one is honest there
        // and demanding zero would be a test of one driver's rounding.
        TEST_CHECK(ctx, worst <= allowedDifference);
        FLUXION_LOG_INFO("RenderCoreTests", "%s: %s decoded by the driver differs from this engine's decoder by at most %d.",
            backendName, formatName, worst);

        Fluxion_RenderGraph_Destroy(graph);
        Fluxion_RHI_DestroyFence(fence);
        Fluxion_RHI_DestroyCommandList(cmd);
        Fluxion_RHI_DestroyBuffer(readback);
        Fluxion_RenderView_Destroy(view);
        Fluxion_RenderTarget_Destroy(target);
        Fluxion_Renderer_Destroy(renderer);
        Fluxion_RenderPipeline_Destroy(pipeline);
        Fluxion_MeshBuffer_Destroy(mesh);
        Fluxion_Material_Destroy(material);
        Fluxion_ShaderProgram_Destroy(program);
    }

    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(depth);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTexture(color);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_RHI_DestroyTextureView(compressedView);
    Fluxion_RHI_DestroyTexture(compressed);
    Fluxion_RHI_Device_CollectGarbage(fixture.device);
}

void CompareOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    GpuFixture fixture = CreateGpuFixture(backend, backendName);
    if (!fixture.usable)
    {
        DestroyGpuFixture(fixture);
        return;
    }

    Fluxion_RenderGraphPassRegistry_Init();

    CompareOneFormatAgainstTheHardware(ctx, fixture, FLUXION_RHI_FORMAT_BC7_UNORM, "BC7_UNORM", backendName, 0);
    CompareOneFormatAgainstTheHardware(ctx, fixture, FLUXION_RHI_FORMAT_BC5_UNORM, "BC5_UNORM", backendName, 1);
    CompareOneFormatAgainstTheHardware(ctx, fixture, FLUXION_RHI_FORMAT_BC4_UNORM, "BC4_UNORM", backendName, 1);
    CompareOneFormatAgainstTheHardware(ctx, fixture, FLUXION_RHI_FORMAT_BC6H_UFLOAT, "BC6H_UFLOAT", backendName, 0);

    // ASTC is here so that the first device which supports it checks this
    // encoder without anyone remembering to ask. No desktop GPU does --
    // neither the discrete one this was written against nor the software
    // rasteriser -- so on a desktop it skips and says so, which is the
    // honest reading of "not checked" rather than "checked and fine".
    CompareOneFormatAgainstTheHardware(ctx, fixture, FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, "ASTC_4X4_UNORM", backendName, 0);

    Fluxion_RenderGraphPassRegistry_Shutdown();
    DestroyGpuFixture(fixture);
}

} // namespace

extern "C" void Test_BlockCompressGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        // No compiler means no shader, and without a shader there is
        // nothing to sample the texture with. Said out loud: this check
        // is the only one that can catch a wrong bit layout, so a machine
        // where it does not run has not checked that.
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the block-decode comparison did not run.");
        return;
    }

    CompareOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CompareOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");

    // OpenGL is left out: this backend reaches a real context through a
    // window, and there is none here.
}
