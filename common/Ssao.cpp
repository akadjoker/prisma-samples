#include "Ssao.h"

#include "GltfModel.h"
#include "depth_only.frag.h"
#include "depth_only.vert.h"
#include "fullscreen.vert.h"
#include "mipmap_depth.frag.h"
#include "prisma/rhi/ShaderBlob.h"
#include "sao.frag.h"
#include "ssao_blur.frag.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stddef.h>
#include <string.h>

namespace zenapp
{

namespace
{

prisma::ShaderHandle makeShader(prisma::Driver* driver, const prisma::ShaderBlob& blob)
{
    return driver->createShader(prisma::shaderDesc(blob, driver->caps()));
}

} // namespace

bool Ssao::valid() const
{
    bool ok = depthSampler.valid() && nearestSampler.valid() && linearSampler.valid() &&
              uniforms.valid() && structure.valid() && structureDouble.valid() && mip.valid() &&
              sao.valid() && blur.valid();
    for (unsigned i = 0; i < kLevels; ++i) ok = ok && depth[i].valid();
    for (unsigned i = 0; i < 3; ++i) ok = ok && ao[i].valid();
    return ok;
}

bool createSsao(prisma::Driver* driver, const SsaoDesc& desc, Ssao* out)
{
    *out = Ssao();
    out->enabled = desc.enabled;

    const unsigned width = static_cast<unsigned>(ceilf(static_cast<float>(desc.width) * 0.5f));
    const unsigned height = static_cast<unsigned>(ceilf(static_cast<float>(desc.height) * 0.5f));
    unsigned maxLevels = 1;
    for (unsigned m = width > height ? width : height; m > 1; m >>= 1) ++maxLevels;
    const unsigned levels = maxLevels - 5 < 8 ? maxLevels - 5 : 8;
    if (levels != Ssao::kLevels) return false;

    for (unsigned level = 0; level < Ssao::kLevels; ++level)
    {
        prisma::TextureDesc depthDesc;
        depthDesc.format = prisma::TextureFormat::Depth32F;
        depthDesc.width = (width >> level) > 1 ? (width >> level) : 1;
        depthDesc.height = (height >> level) > 1 ? (height >> level) : 1;
        depthDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        depthDesc.debugName = "structure depth";
        out->depth[level] = driver->createTexture(depthDesc);
    }
    for (int i = 0; i < 3; ++i)
    {
        prisma::TextureDesc aoDesc;
        aoDesc.format = prisma::TextureFormat::RGBA8;
        aoDesc.width = width;
        aoDesc.height = height;
        aoDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        aoDesc.debugName = "ssao";
        out->ao[i] = driver->createTexture(aoDesc);
    }
    prisma::SamplerDesc depthSamplerDesc;
    depthSamplerDesc.minFilter = prisma::Filter::Nearest;
    depthSamplerDesc.magFilter = prisma::Filter::Nearest;
    depthSamplerDesc.mipFilter = prisma::MipFilter::Nearest;
    depthSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    depthSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    depthSamplerDesc.debugName = "structure sampler";
    out->depthSampler = driver->createSampler(depthSamplerDesc);
    prisma::SamplerDesc nearestDesc = depthSamplerDesc;
    nearestDesc.mipFilter = prisma::MipFilter::None;
    nearestDesc.debugName = "ssao nearest sampler";
    out->nearestSampler = driver->createSampler(nearestDesc);
    prisma::SamplerDesc linearDesc = nearestDesc;
    linearDesc.minFilter = prisma::Filter::Linear;
    linearDesc.magFilter = prisma::Filter::Linear;
    linearDesc.debugName = "ssao linear sampler";
    out->linearSampler = driver->createSampler(linearDesc);

    const float tau = 6.28318531f;
    const float* ip = desc.inverseProjection;
    const float radius = 0.3f;
    const float sampleCount = 7.0f;
    const float spiralTurns = 3.0f;
    const float peak = 0.1f * radius;
    const float increment = (1.0f / (sampleCount - 0.5f)) * spiralTurns * tau;
    const float projectionScale = fminf(0.5f * desc.projection[0] * static_cast<float>(width),
            0.5f * desc.projection[5] * static_cast<float>(height));

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    out->stride = (sizeof(float) * 28 + alignment - 1) / alignment * alignment;
    out->paramsRange = 2 + levels;
    ct::Vector<unsigned char> bytes;
    bytes.resize(static_cast<size_t>(out->stride) * (3 + levels));
    memset(bytes.data(), 0, bytes.size());
    float sao[28] = {
        ip[10], ip[14], ip[11], ip[15],
        static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height),
        ip[0] * 2.0f, ip[5] * 2.0f, 1.0f / (radius * radius), 0.0f,
        peak * peak, projectionScale, projectionScale * radius, 0.0005f,
        2.0f, (tau * peak) / sampleCount, spiralTurns, -1.0f / desc.farPlane,
        sampleCount, 1.0f / (sampleCount - 0.5f), cosf(increment), sinf(increment),
        static_cast<float>(levels - 1), 0.0f, 0.0f, 0.0f };
    memcpy(bytes.data(), sao, sizeof(sao));
    const float standardDeviation = 4.0f;
    float blur[24];
    memset(blur, 0, sizeof(blur));
    const unsigned taps = 6;
    for (unsigned i = 0; i < taps; ++i)
        blur[8 + i] = expf(-(static_cast<float>(i) * static_cast<float>(i)) /
                           (2.0f * standardDeviation * standardDeviation));
    blur[4] = static_cast<float>(taps);
    blur[5] = -desc.farPlane / 0.05f;
    blur[0] = 2.0f / static_cast<float>(width);
    memcpy(bytes.data() + static_cast<size_t>(out->stride), blur, sizeof(blur));
    blur[0] = 0.0f;
    blur[1] = 2.0f / static_cast<float>(height);
    memcpy(bytes.data() + static_cast<size_t>(out->stride) * 2, blur, sizeof(blur));
    for (unsigned level = 0; level + 1 < levels; ++level)
    {
        const float source[4] = { static_cast<float>(level), 0.0f, 0.0f, 0.0f };
        memcpy(bytes.data() + static_cast<size_t>(out->stride) * (3 + level), source, sizeof(source));
    }
    const float params[4] = { desc.enabled ? 1.0f : 0.0f, desc.debug ? 1.0f : 0.0f, 0.0f, 0.0f };
    memcpy(bytes.data() + static_cast<size_t>(out->stride) * out->paramsRange, params, sizeof(params));
    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(bytes.size());
    bufferDesc.data = bytes.data();
    bufferDesc.debugName = "ssao uniforms";
    out->uniforms = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle fullscreen = makeShader(driver, fullscreen_vert);
    const prisma::ShaderHandle depthVertex = makeShader(driver, depth_only_vert);
    const prisma::ShaderHandle depthFragment = makeShader(driver, depth_only_frag);
    const prisma::ShaderHandle mipFragment = makeShader(driver, mipmap_depth_frag);
    const prisma::ShaderHandle saoFragment = makeShader(driver, sao_frag);
    const prisma::ShaderHandle blurFragment = makeShader(driver, ssao_blur_frag);

    prisma::PipelineDesc structureDesc;
    structureDesc.vertexShader = depthVertex;
    structureDesc.fragmentShader = depthFragment;
    structureDesc.vertexBuffers[0].stride = sizeof(GltfVertex);
    structureDesc.vertexBufferCount = 1;
    structureDesc.attributeCount = 1;
    structureDesc.attributes[0].location = 0;
    structureDesc.attributes[0].format = prisma::VertexFormat::Float3;
    structureDesc.attributes[0].offset = offsetof(GltfVertex, position);
    structureDesc.depthTest = true;
    structureDesc.cullMode = prisma::CullMode::Back;
    structureDesc.colorMask = 0;
    structureDesc.targets.window = false;
    structureDesc.targets.colorCount = 0;
    structureDesc.targets.depth = prisma::TextureFormat::Depth32F;
    structureDesc.debugName = "structure";
    out->structure = driver->createPipeline(structureDesc);
    structureDesc.cullMode = prisma::CullMode::None;
    structureDesc.debugName = "structure double sided";
    out->structureDouble = driver->createPipeline(structureDesc);

    prisma::PipelineDesc mipDesc;
    mipDesc.vertexShader = fullscreen;
    mipDesc.fragmentShader = mipFragment;
    mipDesc.depthTest = true;
    mipDesc.depthWrite = true;
    mipDesc.depthCompare = prisma::CompareOp::Always;
    mipDesc.colorMask = 0;
    mipDesc.targets.window = false;
    mipDesc.targets.colorCount = 0;
    mipDesc.targets.depth = prisma::TextureFormat::Depth32F;
    mipDesc.debugName = "structure mip";
    out->mip = driver->createPipeline(mipDesc);

    prisma::PipelineDesc saoDesc;
    saoDesc.vertexShader = fullscreen;
    saoDesc.fragmentShader = saoFragment;
    saoDesc.depthWrite = false;
    saoDesc.targets.window = false;
    saoDesc.targets.colorCount = 1;
    saoDesc.targets.colors[0] = prisma::TextureFormat::RGBA8;
    saoDesc.targets.depth = prisma::TextureFormat::None;
    saoDesc.debugName = "sao";
    out->sao = driver->createPipeline(saoDesc);
    prisma::PipelineDesc blurDesc = saoDesc;
    blurDesc.fragmentShader = blurFragment;
    blurDesc.debugName = "ssao blur";
    out->blur = driver->createPipeline(blurDesc);

    driver->destroy(fullscreen);
    driver->destroy(depthVertex);
    driver->destroy(depthFragment);
    driver->destroy(mipFragment);
    driver->destroy(saoFragment);
    driver->destroy(blurFragment);
    return out->valid();
}

void renderSsao(prisma::Driver* driver, const Ssao& ssao, const SsaoDrawStructure& drawStructure)
{
    if (!ssao.enabled) return;

    prisma::RenderPassDesc structurePass;
    structurePass.depth.texture = ssao.depth[0];
    driver->beginRenderPass(structurePass);
    drawStructure(ssao.structure, ssao.structureDouble);
    driver->endRenderPass();

    for (unsigned level = 1; level < Ssao::kLevels; ++level)
    {
        prisma::RenderPassDesc mipPass;
        mipPass.depth.texture = ssao.depth[level];
        mipPass.depthLoad = prisma::LoadOp::DontCare;
        driver->beginRenderPass(mipPass);
        driver->bindPipeline(ssao.mip);
        driver->bindUniformBuffer(0, ssao.uniforms, (3 + level - 1) * ssao.stride, sizeof(float) * 4);
        driver->bindTexture(0, ssao.depth[level - 1], ssao.depthSampler);
        driver->draw(3, 0);
        driver->endRenderPass();
    }

    prisma::RenderPassDesc aoPass;
    aoPass.colorCount = 1;
    aoPass.depthLoad = prisma::LoadOp::DontCare;
    aoPass.stencilLoad = prisma::LoadOp::DontCare;
    aoPass.clearColor[0] = aoPass.clearColor[1] = aoPass.clearColor[2] = 1.0f;
    aoPass.colors[0].texture = ssao.ao[0];
    driver->beginRenderPass(aoPass);
    driver->bindPipeline(ssao.sao);
    driver->bindUniformBuffer(0, ssao.uniforms, 0, sizeof(float) * 28);
    for (unsigned level = 0; level < Ssao::kLevels; ++level)
        driver->bindTexture(level, ssao.depth[level], ssao.depthSampler);
    driver->draw(3, 0);
    driver->endRenderPass();

    for (int pass = 0; pass < 2; ++pass)
    {
        aoPass.colors[0].texture = ssao.ao[pass + 1];
        driver->beginRenderPass(aoPass);
        driver->bindPipeline(ssao.blur);
        driver->bindUniformBuffer(0, ssao.uniforms, (1 + pass) * ssao.stride, sizeof(float) * 24);
        driver->bindTexture(0, ssao.ao[pass], ssao.nearestSampler);
        driver->draw(3, 0);
        driver->endRenderPass();
    }
}

void bindSsao(prisma::Driver* driver, const Ssao& ssao)
{
    driver->bindUniformBuffer(9, ssao.uniforms, ssao.paramsRange * ssao.stride, sizeof(float) * 4);
    driver->bindTexture(8, ssao.ao[2], ssao.linearSampler);
}

void destroySsao(prisma::Driver* driver, Ssao* ssao)
{
    driver->destroy(ssao->blur);
    driver->destroy(ssao->sao);
    driver->destroy(ssao->mip);
    driver->destroy(ssao->structureDouble);
    driver->destroy(ssao->structure);
    for (int i = 0; i < 3; ++i) driver->destroy(ssao->ao[i]);
    for (unsigned level = 0; level < Ssao::kLevels; ++level) driver->destroy(ssao->depth[level]);
    driver->destroy(ssao->uniforms);
    driver->destroy(ssao->linearSampler);
    driver->destroy(ssao->nearestSampler);
    driver->destroy(ssao->depthSampler);
    *ssao = Ssao();
}

} // namespace zenapp
