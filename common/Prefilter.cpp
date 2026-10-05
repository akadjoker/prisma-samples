#include "Prefilter.h"

#include "prefilter.frag.h"
#include "prefilter.vert.h"
#include "prisma/rhi/ShaderBlob.h"

#include <ct/vector.hpp>

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

struct PrefilterUniforms
{
    float a[4];
    float b[4];
};

unsigned alignUp(unsigned value, unsigned alignment)
{
    return alignment ? (value + alignment - 1) / alignment * alignment : value;
}

unsigned countTrailingZeros(unsigned value)
{
    unsigned count = 0;
    while (value && !(value & 1u))
    {
        value >>= 1;
        ++count;
    }
    return count;
}

bool isPowerOfTwo(unsigned value)
{
    return value && !(value & (value - 1));
}

} // namespace

float lodToPerceptualRoughness(float lod)
{
    const float a = 2.0f;
    const float b = -1.0f;
    if (lod == 0.0f) return 0.0f;
    float value = (sqrtf(a * a + 4.0f * b * lod) - a) / (2.0f * b);
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return value;
}

bool prefilterEnvironment(prisma::Driver* driver, prisma::TextureHandle source,
        unsigned sourceSize, unsigned sourceLevels, const PrefilterDesc& desc,
        PrefilteredEnvironment* out)
{
    if (!driver->caps().floatColorTargets || !source.valid() || sourceLevels == 0) return false;
    if (!isPowerOfTwo(desc.size) || !isPowerOfTwo(desc.minSize) || desc.minSize > desc.size)
        return false;

    const unsigned baseExponent = countTrailingZeros(desc.size);
    unsigned minExponent = countTrailingZeros(desc.minSize);
    if (minExponent >= baseExponent) minExponent = 0;
    const unsigned levels = baseExponent + 1 - minExponent;

    prisma::TextureDesc textureDesc;
    textureDesc.type = prisma::TextureType::TextureCube;
    textureDesc.format = prisma::TextureFormat::RGBA16F;
    textureDesc.width = desc.size;
    textureDesc.height = desc.size;
    textureDesc.mipLevels = levels;
    textureDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    textureDesc.debugName = "prefiltered specular";
    const prisma::TextureHandle texture = driver->createTexture(textureDesc);
    if (!texture.valid()) return false;

    const unsigned stride = alignUp(sizeof(PrefilterUniforms),
            driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(static_cast<size_t>(stride) * levels * 6);
    memset(uniforms.data(), 0, uniforms.size());

    const float omegaP = 4.0f * 3.14159265358979f /
                         static_cast<float>(6.0 * sourceSize * sourceSize);
    const float log4OmegaP = 0.5f * log2f(omegaP);
    float sampleCount = static_cast<float>(desc.sampleCount);
    for (unsigned level = 0; level < levels; ++level)
    {
        if (level >= 2) sampleCount *= 2.0f;
        const float lod = levels > 1 ? static_cast<float>(level) / static_cast<float>(levels - 1) : 0.0f;
        const float perceptual = lodToPerceptualRoughness(lod);
        for (unsigned face = 0; face < 6; ++face)
        {
            PrefilterUniforms block;
            block.a[0] = static_cast<float>(face);
            block.a[1] = perceptual * perceptual;
            block.a[2] = sampleCount;
            block.a[3] = static_cast<float>(sourceLevels - 1);
            block.b[0] = log4OmegaP;
            block.b[1] = block.b[2] = block.b[3] = 0.0f;
            memcpy(uniforms.data() + static_cast<size_t>(level * 6 + face) * stride, &block,
                    sizeof(block));
        }
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(uniforms.size());
    bufferDesc.data = uniforms.data();
    bufferDesc.debugName = "prefilter uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressW = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "prefilter source";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    prisma::ShaderDesc vertexDesc = prisma::shaderDesc(prefilter_vert, driver->caps());
    vertexDesc.debugName = "prefilter vertex";
    prisma::ShaderDesc fragmentDesc = prisma::shaderDesc(prefilter_frag, driver->caps());
    fragmentDesc.debugName = "prefilter fragment";
    const prisma::ShaderHandle vertexShader = driver->createShader(vertexDesc);
    const prisma::ShaderHandle fragmentShader = driver->createShader(fragmentDesc);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthWrite = false;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 1;
    pipelineDesc.targets.colors[0] = prisma::TextureFormat::RGBA16F;
    pipelineDesc.debugName = "prefilter pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = uniformBuffer.valid() && sampler.valid() && pipeline.valid();
    if (ready)
    {
        driver->beginFrame();
        for (unsigned level = 0; level < levels; ++level)
        {
            for (unsigned face = 0; face < 6; ++face)
            {
                prisma::RenderPassDesc pass;
                pass.colors[0].texture = texture;
                pass.colors[0].mip = level;
                pass.colors[0].layer = face;
                pass.colorCount = 1;
                pass.depthLoad = prisma::LoadOp::DontCare;
                pass.stencilLoad = prisma::LoadOp::DontCare;
                driver->beginRenderPass(pass);
                driver->bindPipeline(pipeline);
                driver->bindUniformBuffer(0, uniformBuffer,
                        (level * 6 + face) * stride, sizeof(PrefilterUniforms));
                driver->bindTexture(0, source, sampler);
                driver->draw(3, 0);
                driver->endRenderPass();
            }
        }
        driver->endFrame();
        driver->present();
    }

    driver->destroy(pipeline);
    driver->destroy(sampler);
    driver->destroy(uniformBuffer);
    if (!ready)
    {
        driver->destroy(texture);
        return false;
    }
    out->texture = texture;
    out->size = desc.size;
    out->levels = levels;
    return true;
}

} // namespace zenapp
