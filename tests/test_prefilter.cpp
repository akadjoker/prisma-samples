#include "Check.h"
#include "Dfg.h"
#include "Environment.h"
#include "GpuContext.h"
#include "Prefilter.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include "no_buffer.vert.h"
#include "probe.frag.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

int messages = 0;
char lastMessage[256];

void captureLog(const char* message)
{
    ++messages;
    strncpy(lastMessage, message, sizeof(lastMessage) - 1);
    lastMessage[sizeof(lastMessage) - 1] = '\0';
}

const float kBase = 1.0f;
const float kSlope[3] = { 0.5f, -0.3f, 0.4f };

float environmentValue(const float* d)
{
    return kBase + kSlope[0] * d[0] + kSlope[1] * d[1] + kSlope[2] * d[2];
}

float expectedLobeMean(float linearRoughness, unsigned sampleCount)
{
    double sum = 0.0;
    double weight = 0.0;
    const float inverse = 1.0f / static_cast<float>(sampleCount);
    for (unsigned i = 0; i < sampleCount; ++i)
    {
        float u[2];
        float h[3];
        zenapp::ibl::hammersley(i, inverse, u);
        zenapp::ibl::importanceSampleGgx(u, linearRoughness, h);
        const float nol = 2.0f * h[2] * h[2] - 1.0f;
        if (nol > 0.0f)
        {
            sum += static_cast<double>(nol) * nol;
            weight += nol;
        }
    }
    return static_cast<float>(sum / weight);
}

} // namespace

int main(int argc, char** argv)
{
    using namespace prisma;

    GpuContext gpu;
    if (!gpu.open(argc, argv, captureLog)) return 1;
    Driver* driver = gpu.driver;

    if (!driver->caps().floatColorTargets)
    {
        printf("test_prefilter: float colour targets are not available, skipped\n");
        gpu.close();
        return 0;
    }

    const unsigned sourceSize = 64;
    zenapp::EnvironmentFaces environment;
    environment.size = sourceSize;
    environment.data.resize(static_cast<size_t>(sourceSize) * sourceSize * 24);
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned y = 0; y < sourceSize; ++y)
        {
            for (unsigned x = 0; x < sourceSize; ++x)
            {
                float d[3];
                zenapp::ibl::faceDirection(face, sourceSize, x, y, d);
                const float v = environmentValue(d);
                float* texel = environment.data.data() +
                               ((static_cast<size_t>(face) * sourceSize + y) * sourceSize + x) * 4;
                texel[0] = texel[1] = texel[2] = v;
                texel[3] = 1.0f;
            }
        }
    }

    const TextureHandle source = zenapp::createEnvironmentCubemap(driver, environment, "source");
    CHECK(source.valid());
    const unsigned sourceLevels = zenapp::mipLevelCount(sourceSize);

    zenapp::PrefilterDesc prefilter;
    prefilter.size = 64;
    prefilter.minSize = 16;
    prefilter.sampleCount = 256;
    zenapp::PrefilteredEnvironment filtered;
    messages = 0;
    CHECK(zenapp::prefilterEnvironment(driver, source, sourceSize, sourceLevels, prefilter,
            &filtered));
    CHECK(messages == 0);
    if (messages) printf("unexpected: %s\n", lastMessage);
    CHECK(filtered.levels == 3);
    CHECK(filtered.size == 64);

    TextureDesc probeDesc;
    probeDesc.width = 4;
    probeDesc.height = 4;
    probeDesc.usage = kTextureSampled | kTextureRenderTarget;
    const TextureHandle probe = driver->createTexture(probeDesc);

    struct ProbeBlock
    {
        float direction[4];
        float level[4];
    };
    const unsigned stride = (sizeof(ProbeBlock) + driver->caps().uniformBufferOffsetAlignment - 1) /
                            driver->caps().uniformBufferOffsetAlignment *
                            driver->caps().uniformBufferOffsetAlignment;
    BufferDesc probeBufferDesc;
    probeBufferDesc.usage = BufferUsage::Uniform;
    probeBufferDesc.size = stride;
    probeBufferDesc.update = BufferUpdate::Stream;
    const BufferHandle probeBuffer = driver->createBuffer(probeBufferDesc);

    SamplerDesc samplerDesc;
    samplerDesc.mipFilter = MipFilter::Nearest;
    samplerDesc.addressU = AddressMode::ClampToEdge;
    samplerDesc.addressV = AddressMode::ClampToEdge;
    samplerDesc.addressW = AddressMode::ClampToEdge;
    const SamplerHandle sampler = driver->createSampler(samplerDesc);

    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(probe_frag, driver->caps());
    const ShaderHandle vertexShader = driver->createShader(vertexDesc);
    const ShaderHandle fragmentShader = driver->createShader(fragmentDesc);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 1;
    pipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
    const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);
    CHECK(probe.valid() && probeBuffer.valid() && sampler.valid() && pipeline.valid());
    if (!(probe.valid() && probeBuffer.valid() && sampler.valid() && pipeline.valid()))
    {
        gpu.close();
        return 1;
    }

    const float inverseRoot3 = 0.57735026919f;
    const float inverseRoot2 = 0.70710678118f;
    const float directions[14][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 }, { inverseRoot3, inverseRoot3, inverseRoot3 },
        { -inverseRoot3, inverseRoot3, -inverseRoot3 },
        { inverseRoot3, -inverseRoot3, -inverseRoot3 },
        { -inverseRoot3, -inverseRoot3, inverseRoot3 }, { inverseRoot2, inverseRoot2, 0 },
        { 0, inverseRoot2, -inverseRoot2 }, { -inverseRoot2, 0, inverseRoot2 },
        { 0.8f, -0.6f, 0.0f } };

    float previousMean = 2.0f;
    float sampleCount = static_cast<float>(prefilter.sampleCount);
    for (unsigned level = 0; level < filtered.levels; ++level)
    {
        if (level >= 2) sampleCount *= 2.0f;
        float values[14];
        for (int i = 0; i < 14; ++i)
        {
            ProbeBlock block;
            memcpy(block.direction, directions[i], sizeof(float) * 3);
            block.direction[3] = 0.0f;
            block.level[0] = static_cast<float>(level);
            block.level[1] = block.level[2] = block.level[3] = 0.0f;
            driver->beginFrame();
            driver->updateBuffer(probeBuffer, 0, &block, sizeof(block));
            RenderPassDesc pass;
            pass.colors[0].texture = probe;
            pass.colorCount = 1;
            driver->beginRenderPass(pass);
            driver->bindPipeline(pipeline);
            driver->bindUniformBuffer(0, probeBuffer, 0, sizeof(ProbeBlock));
            driver->bindTexture(0, filtered.texture, sampler);
            driver->draw(3, 0);
            driver->endRenderPass();
            unsigned char pixel[4] = { 0, 0, 0, 0 };
            Rect rect;
            rect.x = 1;
            rect.y = 1;
            rect.width = 1;
            rect.height = 1;
            RenderTarget target;
            target.texture = probe;
            CHECK(driver->readPixels(target, rect, pixel));
            driver->endFrame();
            driver->present();
            values[i] = static_cast<float>(pixel[0]) / 255.0f * 2.0f;
        }

        float numerator = 0.0f;
        float denominator = 0.0f;
        for (int i = 0; i < 14; ++i)
        {
            const float projection = kSlope[0] * directions[i][0] + kSlope[1] * directions[i][1] +
                                     kSlope[2] * directions[i][2];
            numerator += (values[i] - kBase) * projection;
            denominator += projection * projection;
        }
        const float mean = numerator / denominator;
        float residual = 0.0f;
        for (int i = 0; i < 14; ++i)
        {
            const float projection = kSlope[0] * directions[i][0] + kSlope[1] * directions[i][1] +
                                     kSlope[2] * directions[i][2];
            const float error = fabsf(values[i] - (kBase + mean * projection));
            residual = error > residual ? error : residual;
        }

        const float lod = static_cast<float>(level) / static_cast<float>(filtered.levels - 1);
        const float perceptual = zenapp::lodToPerceptualRoughness(lod);
        const float expected = level == 0 ? 1.0f : expectedLobeMean(perceptual * perceptual,
                                                     static_cast<unsigned>(sampleCount));
        printf("level %u: lobe mean %.3f expected %.3f, worst residual %.3f\n", level, mean,
                expected, residual);
        CHECK(residual < 0.03f);
        CHECK(fabsf(mean - expected) < 0.05f);
        CHECK(mean < previousMean);
        previousMean = mean;
    }

    driver->destroy(pipeline);
    driver->destroy(sampler);
    driver->destroy(probeBuffer);
    driver->destroy(probe);
    driver->destroy(filtered.texture);
    driver->destroy(source);
    gpu.close();

    printf(failures ? "test_prefilter: %d failures\n" : "test_prefilter: all passed\n", failures);
    return failures ? 1 : 0;
}
