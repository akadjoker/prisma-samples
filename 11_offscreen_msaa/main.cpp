#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msaa_cube.frag.h"
#include "msaa_cube.vert.h"
#include "msaa_quad.frag.h"
#include "msaa_quad.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float color[3];
};

const float kFaces[6][3][3] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
};

const float kFaceColors[6][3] = {
    { 0.9f, 0.2f, 0.2f },
    { 0.2f, 0.8f, 0.3f },
    { 0.2f, 0.4f, 0.9f },
    { 0.9f, 0.8f, 0.2f },
    { 0.8f, 0.3f, 0.8f },
    { 0.2f, 0.8f, 0.8f },
};

const float kCornerSigns[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

const float kQuad[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

const std::uint32_t kTargetSize = 512;

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 11 offscreen msaa", driverType);
    if (!window)
    {
        log_error("window: %s", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::Driver* driver = zenapp::createDriver(window, driverType);
    if (!driver)
    {
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    const std::uint32_t maxSamples = driver->caps().maxSamples;
    const std::uint32_t samples = maxSamples < 4 ? (maxSamples < 1 ? 1 : maxSamples) : 4;
    const bool resolve = samples > 1;

    char title[96];
    snprintf(title, sizeof(title), "prisma 11 offscreen msaa | %ux", samples);
    window_set_title(window, title);

    Vertex vertices[24];
    std::uint16_t indices[36];
    for (int face = 0; face < 6; ++face)
    {
        for (int corner = 0; corner < 4; ++corner)
        {
            Vertex& vertex = vertices[face * 4 + corner];
            for (int c = 0; c < 3; ++c)
            {
                vertex.position[c] = kFaces[face][0][c] +
                                     kFaces[face][1][c] * kCornerSigns[corner][0] +
                                     kFaces[face][2][c] * kCornerSigns[corner][1];
                vertex.color[c] = kFaceColors[face][c];
            }
        }
        const std::uint16_t first = static_cast<std::uint16_t>(face * 4);
        const std::uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) indices[face * 6 + i] = first + order[i];
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(vertices);
    bufferDesc.data = vertices;
    bufferDesc.debugName = "cube vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = sizeof(indices);
    bufferDesc.data = indices;
    bufferDesc.debugName = "cube indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Vertex;
    bufferDesc.size = sizeof(kQuad);
    bufferDesc.data = kQuad;
    bufferDesc.debugName = "quad vertices";
    const prisma::BufferHandle quadBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(Math::Mat4);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "cube uniforms";
    const prisma::BufferHandle cubeUniforms = driver->createBuffer(bufferDesc);
    bufferDesc.debugName = "quad uniforms";
    const prisma::BufferHandle quadUniforms = driver->createBuffer(bufferDesc);

    prisma::TextureDesc targetDesc;
    targetDesc.format = prisma::TextureFormat::RGBA8;
    targetDesc.width = kTargetSize;
    targetDesc.height = kTargetSize;
    targetDesc.samples = samples;
    targetDesc.usage = resolve ? prisma::kTextureRenderTarget
                               : (prisma::kTextureSampled | prisma::kTextureRenderTarget);
    targetDesc.debugName = "offscreen color";
    const prisma::TextureHandle colorTarget = driver->createTexture(targetDesc);

    targetDesc.format = prisma::TextureFormat::Depth32F;
    targetDesc.usage = prisma::kTextureRenderTarget;
    targetDesc.debugName = "offscreen depth";
    const prisma::TextureHandle depthTarget = driver->createTexture(targetDesc);

    prisma::TextureHandle resolveTarget;
    if (resolve)
    {
        targetDesc.format = prisma::TextureFormat::RGBA8;
        targetDesc.samples = 1;
        targetDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        targetDesc.debugName = "offscreen resolve";
        resolveTarget = driver->createTexture(targetDesc);
    }
    const prisma::TextureHandle shown = resolve ? resolveTarget : colorTarget;

    prisma::SamplerDesc samplerDesc;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "offscreen sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const prisma::ShaderHandle cubeVertex = zenapp::createShader(driver, msaa_cube_vert);
    const prisma::ShaderHandle cubeFragment = zenapp::createShader(driver, msaa_cube_frag);
    const prisma::ShaderHandle quadVertex = zenapp::createShader(driver, msaa_quad_vert);
    const prisma::ShaderHandle quadFragment = zenapp::createShader(driver, msaa_quad_frag);

    prisma::PipelineDesc cubeDesc;
    cubeDesc.vertexShader = cubeVertex;
    cubeDesc.fragmentShader = cubeFragment;
    cubeDesc.vertexBuffers[0].stride = sizeof(Vertex);
    cubeDesc.vertexBufferCount = 1;
    cubeDesc.attributeCount = 2;
    cubeDesc.attributes[0].location = 0;
    cubeDesc.attributes[0].format = prisma::VertexFormat::Float3;
    cubeDesc.attributes[0].offset = 0;
    cubeDesc.attributes[1].location = 1;
    cubeDesc.attributes[1].format = prisma::VertexFormat::Float3;
    cubeDesc.attributes[1].offset = sizeof(float) * 3;
    cubeDesc.depthTest = true;
    cubeDesc.cullMode = prisma::CullMode::Back;
    cubeDesc.targets.window = false;
    cubeDesc.targets.colorCount = 1;
    cubeDesc.targets.colors[0] = prisma::TextureFormat::RGBA8;
    cubeDesc.targets.depth = prisma::TextureFormat::Depth32F;
    cubeDesc.targets.samples = samples;
    cubeDesc.debugName = "offscreen cube pipeline";
    const prisma::PipelineHandle cubePipeline = driver->createPipeline(cubeDesc);

    prisma::PipelineDesc quadDesc;
    quadDesc.vertexShader = quadVertex;
    quadDesc.fragmentShader = quadFragment;
    quadDesc.vertexBuffers[0].stride = sizeof(float) * 2;
    quadDesc.vertexBufferCount = 1;
    quadDesc.attributeCount = 1;
    quadDesc.attributes[0].location = 0;
    quadDesc.attributes[0].format = prisma::VertexFormat::Float2;
    quadDesc.attributes[0].offset = 0;
    quadDesc.topology = prisma::Topology::TriangleStrip;
    quadDesc.debugName = "window quad pipeline";
    const prisma::PipelineHandle quadPipeline = driver->createPipeline(quadDesc);

    driver->destroy(cubeVertex);
    driver->destroy(cubeFragment);
    driver->destroy(quadVertex);
    driver->destroy(quadFragment);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && quadBuffer.valid() &&
                       cubeUniforms.valid() && quadUniforms.valid() && colorTarget.valid() &&
                       depthTarget.valid() && shown.valid() && sampler.valid() &&
                       cubePipeline.valid() && quadPipeline.valid();
    if (!ready) log_error("offscreen msaa: resource creation failed");

    prisma::RenderPassDesc offscreenPass;
    offscreenPass.colors[0].texture = colorTarget;
    offscreenPass.colorCount = 1;
    offscreenPass.depth.texture = depthTarget;
    offscreenPass.depthStore = prisma::StoreOp::Discard;
    offscreenPass.clearColor[0] = 0.10f;
    offscreenPass.clearColor[1] = 0.22f;
    offscreenPass.clearColor[2] = 0.35f;
    if (resolve)
    {
        offscreenPass.resolves[0].texture = resolveTarget;
        offscreenPass.colorStore = prisma::StoreOp::Discard;
    }

    prisma::RenderPassDesc windowPass;
    windowPass.clearColor[0] = 0.08f;
    windowPass.clearColor[1] = 0.08f;
    windowPass.clearColor[2] = 0.10f;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float angle = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 cubeProjection = zenapp::perspectiveZeroToOne(1.0f, 1.0f, 0.1f, 100.0f);
        const Math::Mat4 cubeView = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -4.5f));
        const Math::Mat4 cubeModel =
                Math::Mat4::RotationY(angle) * Math::Mat4::RotationX(angle * 0.7f);
        const Math::Mat4 cubeTransform = cubeProjection * cubeView * cubeModel;

        const Math::Mat4 quadProjection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 quadView = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -3.5f));
        const Math::Mat4 quadModel = Math::Mat4::RotationY(angle * 0.5f) *
                                     Math::Mat4::Scale(Math::Vec3(1.2f, 1.2f, 1.2f));
        const Math::Mat4 quadTransform = quadProjection * quadView * quadModel;

        driver->beginFrame();
        driver->updateBuffer(cubeUniforms, 0, &cubeTransform, sizeof(Math::Mat4));
        driver->updateBuffer(quadUniforms, 0, &quadTransform, sizeof(Math::Mat4));

        driver->beginRenderPass(offscreenPass);
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, cubeUniforms, 0, sizeof(Math::Mat4));
        driver->drawIndexed(36, 0);
        driver->endRenderPass();

        driver->beginRenderPass(windowPass);
        driver->bindPipeline(quadPipeline);
        driver->bindVertexBuffer(0, quadBuffer, 0);
        driver->bindUniformBuffer(0, quadUniforms, 0, sizeof(Math::Mat4));
        driver->bindTexture(0, shown, sampler);
        driver->draw(4, 0);
        driver->endRenderPass();

        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(quadPipeline);
    driver->destroy(cubePipeline);
    driver->destroy(sampler);
    if (resolve) driver->destroy(resolveTarget);
    driver->destroy(depthTarget);
    driver->destroy(colorTarget);
    driver->destroy(quadUniforms);
    driver->destroy(cubeUniforms);
    driver->destroy(quadBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
