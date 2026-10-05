#include "common/Projection.h"
#include "common/TextureLoader.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "texture.frag.h"
#include "texture.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float uv[2];
};

struct FrameUniforms
{
    Math::Mat4 modelViewProjection;
    float tint[4];
};

const float kFaces[6][3][3] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
};

const float kCornerSigns[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
const float kCornerUv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };

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

    PlatformWindow* window = zenapp::openWindow("prisma 06 texture", driverType);
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

    char path[1024];
    zenapp::mediaPath("misc/seafloor.dds", path, sizeof(path));
    const prisma::TextureHandle texture = zenapp::loadTexture(driver, path, true, false);
    if (!texture.valid())
    {
        log_error("texture: cannot create %s on this GPU", path);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    float anisotropy = driver->caps().maxAnisotropy;
    if (anisotropy > 16.0f) anisotropy = 16.0f;
    if (anisotropy < 1.0f) anisotropy = 1.0f;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = prisma::Filter::Linear;
    samplerDesc.magFilter = prisma::Filter::Linear;
    samplerDesc.mipFilter = prisma::MipFilter::Linear;
    samplerDesc.maxAnisotropy = anisotropy;
    samplerDesc.debugName = "cube sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

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
            }
            vertex.uv[0] = kCornerUv[corner][0];
            vertex.uv[1] = kCornerUv[corner][1];
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

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, texture_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, texture_frag);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexBuffers[0].stride = sizeof(Vertex);
    pipelineDesc.vertexBufferCount = 1;
    pipelineDesc.attributeCount = 2;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float2;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    pipelineDesc.debugName = "texture pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = sampler.valid() && vertexBuffer.valid() && indexBuffer.valid() &&
                       uniformBuffer.valid() && pipeline.valid();
    if (!ready) log_error("texture: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

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
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -5.0f));
        const Math::Mat4 model = Math::Mat4::RotationY(time) * Math::Mat4::RotationX(time * 0.7f);

        FrameUniforms frame;
        frame.modelViewProjection = projection * view * model;
        frame.tint[0] = 0.75f + 0.25f * sinf(time * 1.1f);
        frame.tint[1] = 0.75f + 0.25f * sinf(time * 1.7f + 2.0f);
        frame.tint[2] = 0.75f + 0.25f * sinf(time * 2.3f + 4.0f);
        frame.tint[3] = 1.0f;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, texture, sampler);
        driver->drawIndexed(36, 0);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    driver->destroy(sampler);
    driver->destroy(texture);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
