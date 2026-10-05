#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <stdlib.h>
#include <string.h>

#include "two_cubes.frag.h"
#include "two_cubes.vert.h"

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

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

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

    PlatformWindow* window = zenapp::openWindow("prisma 04 two cubes", driverType);
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

    const std::uint32_t kObjectCount = 2;
    const std::uint32_t stride =
            alignUp(sizeof(Math::Mat4), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kObjectCount);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kObjectCount;
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "object uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, two_cubes_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, two_cubes_frag);

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
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    pipelineDesc.debugName = "two cubes pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && uniformBuffer.valid() &&
                       pipeline.valid();
    if (!ready) log_error("two cubes: resource creation failed");

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
        const float angle = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -7.5f)) *
                                Math::Mat4::RotationX(0.5f);
        const Math::Mat4 viewProjection = projection * view;

        const Math::Mat4 centerModel =
                Math::Mat4::RotationY(angle) * Math::Mat4::RotationX(angle * 0.7f);
        const Math::Mat4 orbitModel = Math::Mat4::RotationY(angle * 0.8f + 1.6f) *
                                      Math::Mat4::Translation(Math::Vec3(3.5f, 0.0f, 0.0f)) *
                                      Math::Mat4::RotationY(angle * 2.0f) *
                                      Math::Mat4::Scale(Math::Vec3(0.5f, 0.5f, 0.5f));
        const Math::Mat4 transforms[kObjectCount] = { viewProjection * centerModel,
            viewProjection * orbitModel };
        for (std::uint32_t i = 0; i < kObjectCount; ++i)
            memcpy(uniforms.data() + i * stride, &transforms[i], sizeof(Math::Mat4));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kObjectCount);
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        for (std::uint32_t i = 0; i < kObjectCount; ++i)
        {
            driver->bindUniformBuffer(0, uniformBuffer, i * stride, sizeof(Math::Mat4));
            driver->drawIndexed(36, 0);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
