#include "SunShadow.h"

#include "GltfModel.h"
#include "depth_only.frag.h"
#include "depth_only.vert.h"
#include "prisma/rhi/ShaderBlob.h"

#include <stddef.h>
#include <string.h>

namespace zenapp
{

namespace
{

struct FrameBytes
{
    float viewProjection[16];
    float rest[16 + 4 * 4];
};

} // namespace

bool SunShadow::valid() const
{
    return map.valid() && sampler.valid() && uniforms.valid() && lightFrame.valid() &&
           pipeline.valid();
}

bool createSunShadow(prisma::Driver* driver, unsigned size, const SunShadowParams& params,
        SunShadow* out)
{
    *out = SunShadow();

    prisma::BufferDesc uniformDesc;
    uniformDesc.usage = prisma::BufferUsage::Uniform;
    uniformDesc.size = sizeof(SunShadowParams);
    uniformDesc.data = &params;
    uniformDesc.debugName = "sun shadow uniforms";
    out->uniforms = driver->createBuffer(uniformDesc);

    FrameBytes frame;
    memset(&frame, 0, sizeof(frame));
    memcpy(frame.viewProjection, params.matrix, sizeof(frame.viewProjection));
    prisma::BufferDesc frameDesc;
    frameDesc.usage = prisma::BufferUsage::Uniform;
    frameDesc.size = sizeof(frame);
    frameDesc.data = &frame;
    frameDesc.debugName = "light frame uniforms";
    out->lightFrame = driver->createBuffer(frameDesc);

    prisma::TextureDesc mapDesc;
    mapDesc.format = prisma::TextureFormat::Depth32F;
    mapDesc.width = size;
    mapDesc.height = size;
    mapDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    mapDesc.debugName = "sun shadow map";
    out->map = driver->createTexture(mapDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.compare = true;
    samplerDesc.compareOp = prisma::CompareOp::LessEqual;
    samplerDesc.debugName = "sun shadow sampler";
    out->sampler = driver->createSampler(samplerDesc);

    const prisma::ShaderHandle vertex =
            driver->createShader(prisma::shaderDesc(depth_only_vert, driver->caps()));
    const prisma::ShaderHandle fragment =
            driver->createShader(prisma::shaderDesc(depth_only_frag, driver->caps()));
    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertex;
    pipelineDesc.fragmentShader = fragment;
    pipelineDesc.vertexBuffers[0].stride = sizeof(GltfVertex);
    pipelineDesc.vertexBufferCount = 1;
    pipelineDesc.attributeCount = 1;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = offsetof(GltfVertex, position);
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::None;
    pipelineDesc.colorMask = 0;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 0;
    pipelineDesc.targets.depth = prisma::TextureFormat::Depth32F;
    pipelineDesc.depthBiasConstant = 2.0f;
    pipelineDesc.depthBiasSlope = 2.0f;
    pipelineDesc.debugName = "sun shadow";
    out->pipeline = driver->createPipeline(pipelineDesc);
    driver->destroy(vertex);
    driver->destroy(fragment);
    return out->valid();
}

void renderSunShadow(prisma::Driver* driver, SunShadow* shadow, const SunShadowDraw& drawCasters)
{
    if (shadow->drawn) return;
    prisma::RenderPassDesc pass;
    pass.depth.texture = shadow->map;
    driver->beginRenderPass(pass);
    driver->bindPipeline(shadow->pipeline);
    driver->bindUniformBuffer(0, shadow->lightFrame, 0, sizeof(FrameBytes));
    drawCasters(shadow->pipeline);
    driver->endRenderPass();
    shadow->drawn = true;
}

void bindSunShadow(prisma::Driver* driver, const SunShadow& shadow)
{
    driver->bindUniformBuffer(8, shadow.uniforms, 0, sizeof(SunShadowParams));
    driver->bindTexture(7, shadow.map, shadow.sampler);
}

void destroySunShadow(prisma::Driver* driver, SunShadow* shadow)
{
    driver->destroy(shadow->pipeline);
    driver->destroy(shadow->sampler);
    driver->destroy(shadow->map);
    driver->destroy(shadow->lightFrame);
    driver->destroy(shadow->uniforms);
    *shadow = SunShadow();
}

} // namespace zenapp
