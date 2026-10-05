#include "ColorPyramid.h"

#include "fullscreen.vert.h"
#include "gaussian_blur.frag.h"
#include "prisma/rhi/ShaderBlob.h"

#include <ct/vector.hpp>

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

const unsigned kKernelWidth = 21;
const unsigned kKernelStored = 6;
const unsigned kRanges = 2 * (ColorPyramid::kLevels - 1);

float sigma0()
{
    return static_cast<float>(kKernelWidth + 1) / 6.0f;
}

unsigned levelSize(unsigned base, unsigned level)
{
    return (base >> level) > 1 ? (base >> level) : 1;
}

} // namespace

bool ColorPyramid::valid() const
{
    bool ok = sampler.valid() && uniforms.valid() && blur.valid();
    for (unsigned i = 0; i < kLevels; ++i) ok = ok && level[i].valid();
    for (unsigned i = 1; i < kLevels; ++i) ok = ok && temp[i].valid();
    return ok;
}

float colorPyramidLodOffset(float verticalFovRadians, unsigned height)
{
    const float texelSizeAtOneMeter = tanf(verticalFovRadians) / static_cast<float>(height);
    return -log2f(1.41421356f * sigma0() * texelSizeAtOneMeter);
}

bool createColorPyramid(prisma::Driver* driver, unsigned width, unsigned height, ColorPyramid* out)
{
    *out = ColorPyramid();

    unsigned maxLevels = 1;
    for (unsigned m = width > height ? width : height; m > 1; m >>= 1) ++maxLevels;
    unsigned wanted = maxLevels - 4;
    if (wanted < 4) wanted = maxLevels < 4 ? maxLevels : 4;
    if (wanted != ColorPyramid::kLevels) return false;

    for (unsigned i = 0; i < ColorPyramid::kLevels; ++i)
    {
        out->width[i] = levelSize(width, i);
        out->height[i] = levelSize(height, i);
        prisma::TextureDesc desc;
        desc.format = prisma::TextureFormat::RGBA16F;
        desc.width = out->width[i];
        desc.height = out->height[i];
        desc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        desc.debugName = "color pyramid";
        out->level[i] = driver->createTexture(desc);
        if (i >= 1)
        {
            desc.width = out->width[i];
            desc.height = out->height[i - 1];
            desc.debugName = "color pyramid temporary";
            out->temp[i] = driver->createTexture(desc);
        }
    }

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    samplerDesc.magFilter = samplerDesc.minFilter;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "color pyramid sampler";
    out->sampler = driver->createSampler(samplerDesc);

    float kernel[kKernelStored][2];
    const float alpha = 1.0f / (2.0f * sigma0() * sigma0());
    kernel[0][0] = 1.0f;
    kernel[0][1] = 0.0f;
    float totalWeight = kernel[0][0];
    for (unsigned i = 1; i < kKernelStored; ++i)
    {
        const float x0 = static_cast<float>(i * 2 - 1);
        const float x1 = static_cast<float>(i * 2);
        const float k0 = expf(-alpha * x0 * x0);
        const float k1 = expf(-alpha * x1 * x1);
        const float k = k0 + k1;
        kernel[i][0] = k;
        kernel[i][1] = k1 / k;
        totalWeight += (k0 + k1) * 2.0f;
    }
    for (unsigned i = 0; i < kKernelStored; ++i) kernel[i][0] *= 1.0f / totalWeight;

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    out->stride = (sizeof(float) * 28 + alignment - 1) / alignment * alignment;
    ct::Vector<unsigned char> bytes;
    bytes.resize(static_cast<size_t>(out->stride) * kRanges);
    memset(bytes.data(), 0, bytes.size());
    for (unsigned i = 1; i < ColorPyramid::kLevels; ++i)
    {
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            float block[28];
            memset(block, 0, sizeof(block));
            if (pass == 0)
                block[0] = 1.0f / static_cast<float>(out->width[i - 1]);
            else
                block[1] = 1.0f / static_cast<float>(out->height[i - 1]);
            block[2] = i == 1 && pass == 0 ? 1.0f : 0.0f;
            block[3] = static_cast<float>(kKernelStored);
            for (unsigned k = 0; k < kKernelStored; ++k)
            {
                block[4 + k * 4] = kernel[k][0];
                block[4 + k * 4 + 1] = kernel[k][1];
            }
            memcpy(bytes.data() + static_cast<size_t>(out->stride) * (2 * (i - 1) + pass), block,
                    sizeof(block));
        }
    }
    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(bytes.size());
    bufferDesc.data = bytes.data();
    bufferDesc.debugName = "color pyramid uniforms";
    out->uniforms = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertex =
            driver->createShader(prisma::shaderDesc(fullscreen_vert, driver->caps()));
    const prisma::ShaderHandle fragment =
            driver->createShader(prisma::shaderDesc(gaussian_blur_frag, driver->caps()));
    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertex;
    pipelineDesc.fragmentShader = fragment;
    pipelineDesc.depthWrite = false;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 1;
    pipelineDesc.targets.colors[0] = prisma::TextureFormat::RGBA16F;
    pipelineDesc.targets.depth = prisma::TextureFormat::None;
    pipelineDesc.debugName = "color pyramid blur";
    out->blur = driver->createPipeline(pipelineDesc);
    driver->destroy(vertex);
    driver->destroy(fragment);
    return out->valid();
}

void renderColorPyramid(prisma::Driver* driver, const ColorPyramid& pyramid)
{
    prisma::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.depthLoad = prisma::LoadOp::DontCare;
    pass.stencilLoad = prisma::LoadOp::DontCare;
    for (unsigned i = 1; i < ColorPyramid::kLevels; ++i)
    {
        pass.colors[0].texture = pyramid.temp[i];
        driver->beginRenderPass(pass);
        driver->bindPipeline(pyramid.blur);
        driver->bindUniformBuffer(0, pyramid.uniforms, (2 * (i - 1)) * pyramid.stride, sizeof(float) * 28);
        driver->bindTexture(0, pyramid.level[i - 1], pyramid.sampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        pass.colors[0].texture = pyramid.level[i];
        driver->beginRenderPass(pass);
        driver->bindPipeline(pyramid.blur);
        driver->bindUniformBuffer(0, pyramid.uniforms, (2 * (i - 1) + 1) * pyramid.stride,
                sizeof(float) * 28);
        driver->bindTexture(0, pyramid.temp[i], pyramid.sampler);
        driver->draw(3, 0);
        driver->endRenderPass();
    }
}

void bindColorPyramid(prisma::Driver* driver, const ColorPyramid& pyramid, unsigned firstSlot)
{
    for (unsigned i = 0; i < ColorPyramid::kLevels; ++i)
        driver->bindTexture(firstSlot + i, pyramid.level[i], pyramid.sampler);
}

void destroyColorPyramid(prisma::Driver* driver, ColorPyramid* pyramid)
{
    driver->destroy(pyramid->blur);
    driver->destroy(pyramid->uniforms);
    driver->destroy(pyramid->sampler);
    for (unsigned i = 0; i < ColorPyramid::kLevels; ++i)
    {
        driver->destroy(pyramid->level[i]);
        driver->destroy(pyramid->temp[i]);
    }
    *pyramid = ColorPyramid();
}

} // namespace zenapp
