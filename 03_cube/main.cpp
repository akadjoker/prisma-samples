#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <stdlib.h>
#include <string.h>

#include "copy.frag.h"
#include "copy.vert.h"
#include "cube.frag.h"
#include "cube.vert.h"

namespace
{

const float kCoveringTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

struct Corner
{
    float position[3];
    float color[3];
};

struct Vertex
{
    float position[3];
    float color[3];
    float uv[2];
};

const float kCornerUv[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

const Corner kCorners[24] = {
    { { -1, -1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, -1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, 1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { -1, 1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, -1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { -1, -1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { -1, 1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { 1, 1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { 1, -1, 1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, -1, -1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, 1, -1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, 1, 1 }, { 0.2f, 0.4f, 0.9f } },
    { { -1, -1, -1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, -1, 1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, 1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, -1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, 1 }, { 0.8f, 0.3f, 0.8f } },
    { { 1, 1, 1 }, { 0.8f, 0.3f, 0.8f } },
    { { 1, 1, -1 }, { 0.8f, 0.3f, 0.8f } },
    { { -1, 1, -1 }, { 0.8f, 0.3f, 0.8f } },
    { { -1, -1, -1 }, { 0.2f, 0.8f, 0.8f } },
    { { 1, -1, -1 }, { 0.2f, 0.8f, 0.8f } },
    { { 1, -1, 1 }, { 0.2f, 0.8f, 0.8f } },
    { { -1, -1, 1 }, { 0.2f, 0.8f, 0.8f } },
};

const std::uint16_t kIndices[36] = {
    0,
    1,
    2,
    0,
    2,
    3,
    4,
    5,
    6,
    4,
    6,
    7,
    8,
    9,
    10,
    8,
    10,
    11,
    12,
    13,
    14,
    12,
    14,
    15,
    16,
    17,
    18,
    16,
    18,
    19,
    20,
    21,
    22,
    20,
    22,
    23,
};

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool offscreen = zenapp::hasArgument(argc, argv, "offscreen");
    const bool still = zenapp::hasArgument(argc, argv, "still");

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma cube", driverType);
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

    Vertex vertices[24];
    for (int i = 0; i < 24; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            vertices[i].position[c] = kCorners[i].position[c];
            vertices[i].color[c] = kCorners[i].color[c];
        }
        vertices[i].uv[0] = kCornerUv[i % 4][0];
        vertices[i].uv[1] = kCornerUv[i % 4][1];
    }

    const int kTextureSize = 64;
    static unsigned char pixels[kTextureSize * kTextureSize * 4];
    for (int y = 0; y < kTextureSize; ++y)
    {
        for (int x = 0; x < kTextureSize; ++x)
        {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            unsigned char* pixel = &pixels[(y * kTextureSize + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = light ? 255 : 110;
            pixel[3] = 255;
        }
    }

    prisma::TextureDesc textureDesc;
    textureDesc.width = kTextureSize;
    textureDesc.height = kTextureSize;
    textureDesc.mipLevels = 0;
    textureDesc.data = pixels;
    textureDesc.generateMipmaps = true;
    textureDesc.debugName = "cube checker";
    const prisma::TextureHandle texture = driver->createTexture(textureDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.debugName = "cube sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(vertices);
    bufferDesc.data = vertices;
    bufferDesc.debugName = "cube vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = sizeof(kIndices);
    bufferDesc.data = kIndices;
    bufferDesc.debugName = "cube indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(Math::Mat4);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "cube frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, cube_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, cube_frag);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexBuffers[0].stride = sizeof(Vertex);
    pipelineDesc.vertexBufferCount = 1;
    pipelineDesc.attributeCount = 3;
    pipelineDesc.attributes[2].location = 2;
    pipelineDesc.attributes[2].format = prisma::VertexFormat::Float2;
    pipelineDesc.attributes[2].offset = sizeof(float) * 6;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    if (offscreen)
    {
        pipelineDesc.targets.window = false;
        pipelineDesc.targets.colorCount = 1;
        pipelineDesc.targets.colors[0] = driver->caps().floatColorTargets
                                                 ? prisma::TextureFormat::RGBA16F
                                                 : prisma::TextureFormat::RGBA8;
        pipelineDesc.targets.depth = prisma::TextureFormat::Depth32F;
    }
    pipelineDesc.debugName = "cube pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && uniformBuffer.valid() &&
                       pipeline.valid() && texture.valid() && sampler.valid();
    if (!ready) log_error("cube: resource creation failed");

    const std::uint32_t kTargetWidth = 320;
    const std::uint32_t kTargetHeight = 180;
    prisma::TextureHandle colorTarget;
    prisma::TextureHandle depthTarget;
    prisma::SamplerHandle copySampler;
    prisma::BufferHandle copyVertices;
    prisma::PipelineHandle copyPipeline;
    if (offscreen)
    {
        prisma::TextureDesc targetDesc;
        targetDesc.format = driver->caps().floatColorTargets ? prisma::TextureFormat::RGBA16F
                                                             : prisma::TextureFormat::RGBA8;
        targetDesc.width = kTargetWidth;
        targetDesc.height = kTargetHeight;
        targetDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        targetDesc.debugName = "cube color target";
        colorTarget = driver->createTexture(targetDesc);
        targetDesc.format = prisma::TextureFormat::Depth32F;
        targetDesc.usage = prisma::kTextureRenderTarget;
        targetDesc.debugName = "cube depth target";
        depthTarget = driver->createTexture(targetDesc);

        prisma::SamplerDesc copySamplerDesc;
        copySamplerDesc.minFilter = prisma::Filter::Nearest;
        copySamplerDesc.magFilter = prisma::Filter::Nearest;
        copySamplerDesc.mipFilter = prisma::MipFilter::None;
        copySamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
        copySamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
        copySampler = driver->createSampler(copySamplerDesc);

        prisma::BufferDesc copyBufferDesc;
        copyBufferDesc.size = sizeof(kCoveringTriangle);
        copyBufferDesc.data = kCoveringTriangle;
        copyVertices = driver->createBuffer(copyBufferDesc);

        const prisma::ShaderHandle copyVertex = zenapp::createShader(driver, copy_vert);
        const prisma::ShaderHandle copyFragment = zenapp::createShader(driver, copy_frag);

        prisma::PipelineDesc copyDesc;
        copyDesc.vertexShader = copyVertex;
        copyDesc.fragmentShader = copyFragment;
        copyDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        copyDesc.vertexBufferCount = 1;
        copyDesc.attributeCount = 1;
        copyDesc.attributes[0].format = prisma::VertexFormat::Float2;
        copyDesc.debugName = "copy pipeline";
        copyPipeline = driver->createPipeline(copyDesc);
        driver->destroy(copyVertex);
        driver->destroy(copyFragment);
    }

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;
    prisma::RenderPassDesc scenePass = pass;
    if (offscreen)
    {
        scenePass.colors[0].texture = colorTarget;
        scenePass.colorCount = 1;
        scenePass.depth.texture = depthTarget;
        scenePass.depthStore = prisma::StoreOp::Discard;
        pass.depthLoad = prisma::LoadOp::DontCare;
    }

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

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -5.0f));
        const Math::Mat4 model = Math::Mat4::RotationY(angle) * Math::Mat4::RotationX(angle * 0.7f);
        const Math::Mat4 modelViewProjection = projection * view * model;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &modelViewProjection, sizeof(Math::Mat4));
        driver->beginRenderPass(scenePass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(Math::Mat4));
        driver->bindTexture(0, texture, sampler);
        driver->drawIndexed(36, 0);
        driver->endRenderPass();
        if (offscreen)
        {
            driver->beginRenderPass(pass);
            driver->bindPipeline(copyPipeline);
            driver->bindVertexBuffer(0, copyVertices, 0);
            driver->bindTexture(0, colorTarget, copySampler);
            driver->draw(3, 0);
            driver->endRenderPass();
        }
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (offscreen)
    {
        driver->destroy(copyPipeline);
        driver->destroy(copyVertices);
        driver->destroy(copySampler);
        driver->destroy(depthTarget);
        driver->destroy(colorTarget);
    }
    driver->destroy(pipeline);
    driver->destroy(sampler);
    driver->destroy(texture);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
