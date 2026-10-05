#include "PostProcess.h"

#include "bloom_down13.frag.h"
#include "bloom_down2x.frag.h"
#include "bloom_down9.frag.h"
#include "bloom_up.frag.h"
#include "final.frag.h"
#include "fullscreen.vert.h"
#include "fxaa.frag.h"
#include "prisma/rhi/ShaderBlob.h"

#include <ct/vector.hpp>

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

enum
{
    kRangeDown = 0,
    kRangeUp = 1,
    kRangeFinal = 1 + PostProcess::kBloomLevels,
    kRangeFxaa = 2 + PostProcess::kBloomLevels,
    kRangeCount = 3 + PostProcess::kBloomLevels
};

void offscreenFormat(prisma::PipelineDesc* desc, prisma::TextureFormat format)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = format;
    desc->targets.depth = prisma::TextureFormat::None;
}

prisma::ShaderHandle makeShader(prisma::Driver* driver, const prisma::ShaderBlob& blob)
{
    return driver->createShader(prisma::shaderDesc(blob, driver->caps()));
}

} // namespace

bool PostProcess::valid() const
{
    bool ok = sampler.valid() && ldrColor.valid() && uniforms.valid() && down2x.valid() &&
              down13.valid() && down9.valid() && up.valid() && final.valid() && fxaa.valid();
    for (unsigned i = 0; i < kBloomLevels; ++i) ok = ok && bloom[i].valid();
    return ok;
}

bool createPostProcess(prisma::Driver* driver, unsigned width, unsigned height,
        float bloomStrength, bool fxaa, PostProcess* out)
{
    *out = PostProcess();
    out->bloomStrength = bloomStrength;

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    samplerDesc.magFilter = samplerDesc.minFilter;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "post sampler";
    out->sampler = driver->createSampler(samplerDesc);

    prisma::TextureDesc ldrDesc;
    ldrDesc.format = prisma::TextureFormat::RGBA8;
    ldrDesc.width = width;
    ldrDesc.height = height;
    ldrDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    ldrDesc.debugName = "ldr color";
    out->ldrColor = driver->createTexture(ldrDesc);

    const unsigned baseWidth = static_cast<unsigned>(floorf(
            static_cast<float>(PostProcess::kBloomHeight) * static_cast<float>(width) /
            static_cast<float>(height)));
    for (unsigned level = 0; level < PostProcess::kBloomLevels; ++level)
    {
        out->bloomWidth[level] = baseWidth >> level > 1 ? baseWidth >> level : 1;
        out->bloomHeight[level] = PostProcess::kBloomHeight >> level > 1
                ? PostProcess::kBloomHeight >> level : 1;
        prisma::TextureDesc bloomDesc;
        bloomDesc.format = prisma::TextureFormat::RGBA16F;
        bloomDesc.width = out->bloomWidth[level];
        bloomDesc.height = out->bloomHeight[level];
        bloomDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        bloomDesc.debugName = "bloom";
        out->bloom[level] = driver->createTexture(bloomDesc);
    }

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    out->stride = (sizeof(float) * 4 + alignment - 1) / alignment * alignment;
    ct::Vector<unsigned char> bytes;
    bytes.resize(static_cast<size_t>(out->stride) * kRangeCount);
    memset(bytes.data(), 0, bytes.size());
    const float downParams[4] = { 1.0f, 1.0f, 1.0f / 1000.0f, 0.0f };
    memcpy(bytes.data(), downParams, sizeof(downParams));
    for (unsigned level = 0; level + 1 < PostProcess::kBloomLevels; ++level)
    {
        const float w = static_cast<float>(out->bloomWidth[level]);
        const float h = static_cast<float>(out->bloomHeight[level]);
        const float resolution[4] = { w, h, 1.0f / w, 1.0f / h };
        memcpy(bytes.data() + static_cast<size_t>(kRangeUp + level) * out->stride, resolution,
                sizeof(resolution));
    }
    const float finalParams[4] = { bloomStrength, 0.0f, 0.0f, 0.0f };
    memcpy(bytes.data() + static_cast<size_t>(kRangeFinal) * out->stride, finalParams,
            sizeof(finalParams));
    const float fxaaParams[4] = { fxaa ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    memcpy(bytes.data() + static_cast<size_t>(kRangeFxaa) * out->stride, fxaaParams,
            sizeof(fxaaParams));
    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(bytes.size());
    bufferDesc.data = bytes.data();
    bufferDesc.debugName = "post uniforms";
    out->uniforms = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertex = makeShader(driver, fullscreen_vert);
    const prisma::ShaderHandle down2xFragment = makeShader(driver, bloom_down2x_frag);
    const prisma::ShaderHandle down13Fragment = makeShader(driver, bloom_down13_frag);
    const prisma::ShaderHandle down9Fragment = makeShader(driver, bloom_down9_frag);
    const prisma::ShaderHandle upFragment = makeShader(driver, bloom_up_frag);
    const prisma::ShaderHandle finalFragment = makeShader(driver, final_frag);
    const prisma::ShaderHandle fxaaFragment = makeShader(driver, fxaa_frag);

    prisma::PipelineDesc down2xDesc;
    down2xDesc.vertexShader = vertex;
    down2xDesc.fragmentShader = down2xFragment;
    down2xDesc.depthWrite = false;
    offscreenFormat(&down2xDesc, prisma::TextureFormat::RGBA16F);
    down2xDesc.debugName = "bloom down 2x";
    out->down2x = driver->createPipeline(down2xDesc);
    prisma::PipelineDesc down13Desc = down2xDesc;
    down13Desc.fragmentShader = down13Fragment;
    down13Desc.debugName = "bloom down 13";
    out->down13 = driver->createPipeline(down13Desc);
    prisma::PipelineDesc down9Desc = down2xDesc;
    down9Desc.fragmentShader = down9Fragment;
    down9Desc.debugName = "bloom down 9";
    out->down9 = driver->createPipeline(down9Desc);
    prisma::PipelineDesc upDesc = down2xDesc;
    upDesc.fragmentShader = upFragment;
    upDesc.blend = true;
    upDesc.srcColor = prisma::BlendFactor::One;
    upDesc.dstColor = prisma::BlendFactor::One;
    upDesc.srcAlpha = prisma::BlendFactor::One;
    upDesc.dstAlpha = prisma::BlendFactor::One;
    upDesc.debugName = "bloom up";
    out->up = driver->createPipeline(upDesc);

    prisma::PipelineDesc finalDesc;
    finalDesc.vertexShader = vertex;
    finalDesc.fragmentShader = finalFragment;
    finalDesc.depthWrite = false;
    offscreenFormat(&finalDesc, prisma::TextureFormat::RGBA8);
    finalDesc.debugName = "final pipeline";
    out->final = driver->createPipeline(finalDesc);

    prisma::PipelineDesc fxaaDesc;
    fxaaDesc.vertexShader = vertex;
    fxaaDesc.fragmentShader = fxaaFragment;
    fxaaDesc.depthWrite = false;
    fxaaDesc.debugName = "fxaa pipeline";
    out->fxaa = driver->createPipeline(fxaaDesc);

    driver->destroy(vertex);
    driver->destroy(down2xFragment);
    driver->destroy(down13Fragment);
    driver->destroy(down9Fragment);
    driver->destroy(upFragment);
    driver->destroy(finalFragment);
    driver->destroy(fxaaFragment);
    return out->valid();
}

void renderPostProcess(prisma::Driver* driver, const PostProcess& post, prisma::TextureHandle scene,
        const prisma::RenderPassDesc& windowPass)
{
    if (post.bloomStrength > 0.0f)
    {
        prisma::RenderPassDesc bloomPass;
        bloomPass.colorCount = 1;
        bloomPass.depthLoad = prisma::LoadOp::DontCare;
        bloomPass.stencilLoad = prisma::LoadOp::DontCare;

        bloomPass.colors[0].texture = post.bloom[0];
        driver->beginRenderPass(bloomPass);
        driver->bindPipeline(post.down2x);
        driver->bindUniformBuffer(0, post.uniforms, 0, sizeof(float) * 4);
        driver->bindTexture(0, scene, post.sampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        for (unsigned level = 1; level < PostProcess::kBloomLevels; ++level)
        {
            const bool odd = (post.bloomWidth[level - 1] & 1) || (post.bloomHeight[level - 1] & 1);
            bloomPass.colors[0].texture = post.bloom[level];
            driver->beginRenderPass(bloomPass);
            driver->bindPipeline(odd ? post.down13 : post.down9);
            driver->bindTexture(0, post.bloom[level - 1], post.sampler);
            driver->draw(3, 0);
            driver->endRenderPass();
        }

        bloomPass.colorLoad = prisma::LoadOp::Load;
        for (unsigned level = PostProcess::kBloomLevels - 1; level >= 1; --level)
        {
            bloomPass.colors[0].texture = post.bloom[level - 1];
            driver->beginRenderPass(bloomPass);
            driver->bindPipeline(post.up);
            driver->bindUniformBuffer(0, post.uniforms, level * post.stride, sizeof(float) * 4);
            driver->bindTexture(0, post.bloom[level], post.sampler);
            driver->draw(3, 0);
            driver->endRenderPass();
        }
    }

    prisma::RenderPassDesc ldrPass;
    ldrPass.colors[0].texture = post.ldrColor;
    ldrPass.colorCount = 1;
    ldrPass.depthLoad = prisma::LoadOp::DontCare;
    ldrPass.stencilLoad = prisma::LoadOp::DontCare;
    driver->beginRenderPass(ldrPass);
    driver->bindPipeline(post.final);
    driver->bindUniformBuffer(0, post.uniforms, kRangeFinal * post.stride, sizeof(float) * 4);
    driver->bindTexture(0, scene, post.sampler);
    driver->bindTexture(1, post.bloom[0], post.sampler);
    driver->draw(3, 0);
    driver->endRenderPass();

    driver->beginRenderPass(windowPass);
    driver->bindPipeline(post.fxaa);
    driver->bindUniformBuffer(0, post.uniforms, kRangeFxaa * post.stride, sizeof(float) * 4);
    driver->bindTexture(0, post.ldrColor, post.sampler);
    driver->draw(3, 0);
    driver->endRenderPass();
}

void destroyPostProcess(prisma::Driver* driver, PostProcess* post)
{
    driver->destroy(post->up);
    driver->destroy(post->down9);
    driver->destroy(post->down13);
    driver->destroy(post->down2x);
    driver->destroy(post->fxaa);
    driver->destroy(post->final);
    for (unsigned level = 0; level < PostProcess::kBloomLevels; ++level)
        driver->destroy(post->bloom[level]);
    driver->destroy(post->uniforms);
    driver->destroy(post->ldrColor);
    driver->destroy(post->sampler);
    *post = PostProcess();
}

} // namespace zenapp
