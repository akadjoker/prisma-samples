#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "instancing.frag.h"
#include "instancing.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float normal[3];
};

struct Instance
{
    float model[16];
    float color[4];
};

static_assert(sizeof(Instance) == 80, "instance layout");
static_assert(sizeof(Math::Mat4) == 64, "matrix layout");

const int kGridX = 20;
const int kGridY = 20;
const int kGridZ = 10;
const std::uint32_t kInstanceCount = kGridX * kGridY * kGridZ;
const float kSpacing = 3.0f;

const float kFaces[6][3][3] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
};

const float kCornerSigns[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

void fillInstances(ct::Vector<Instance>* instances, float time)
{
    int index = 0;
    for (int z = 0; z < kGridZ; ++z)
    {
        for (int y = 0; y < kGridY; ++y)
        {
            for (int x = 0; x < kGridX; ++x)
            {
                const float phase = static_cast<float>(x) * 0.5f + static_cast<float>(y) * 0.3f +
                                    static_cast<float>(z) * 0.7f;
                const Math::Vec3 position((static_cast<float>(x) - (kGridX - 1) * 0.5f) * kSpacing,
                        (static_cast<float>(z) - (kGridZ - 1) * 0.5f) * kSpacing +
                                0.6f * sinf(time * 2.0f + phase),
                        (static_cast<float>(y) - (kGridY - 1) * 0.5f) * kSpacing);
                const Math::Mat4 model = Math::Mat4::Translation(position) *
                                         Math::Mat4::RotationY(time + phase) *
                                         Math::Mat4::RotationX(time * 0.7f + phase) *
                                         Math::Mat4::Scale(Math::Vec3(0.6f, 0.6f, 0.6f));
                Instance& instance = (*instances)[index++];
                memcpy(instance.model, &model, sizeof(instance.model));
                instance.color[0] = 0.25f + 0.75f * static_cast<float>(x) / (kGridX - 1);
                instance.color[1] = 0.25f + 0.75f * static_cast<float>(y) / (kGridY - 1);
                instance.color[2] = 0.25f + 0.75f * static_cast<float>(z) / (kGridZ - 1);
                instance.color[3] = 1.0f;
            }
        }
    }
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

    PlatformWindow* window = zenapp::openWindow("prisma 10 instancing", driverType);
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
                vertex.normal[c] = kFaces[face][0][c];
            }
        }
        const std::uint16_t first = static_cast<std::uint16_t>(face * 4);
        const std::uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) indices[face * 6 + i] = first + order[i];
    }

    ct::Vector<Instance> instances;
    instances.resize(kInstanceCount);
    fillInstances(&instances, 0.0f);

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
    bufferDesc.size = kInstanceCount * sizeof(Instance);
    bufferDesc.data = instances.data();
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "instance data";
    const prisma::BufferHandle instanceBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(Math::Mat4);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, instancing_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, instancing_frag);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexBuffers[0].stride = sizeof(Vertex);
    pipelineDesc.vertexBuffers[1].stride = sizeof(Instance);
    pipelineDesc.vertexBuffers[1].step = prisma::VertexStep::Instance;
    pipelineDesc.vertexBufferCount = 2;
    pipelineDesc.attributeCount = 7;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[0].buffer = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.attributes[1].buffer = 0;
    for (std::uint32_t column = 0; column < 4; ++column)
    {
        prisma::VertexAttribute& attribute = pipelineDesc.attributes[2 + column];
        attribute.location = 2 + column;
        attribute.format = prisma::VertexFormat::Float4;
        attribute.offset = static_cast<std::uint32_t>(sizeof(float) * 4 * column);
        attribute.buffer = 1;
    }
    pipelineDesc.attributes[6].location = 6;
    pipelineDesc.attributes[6].format = prisma::VertexFormat::Float4;
    pipelineDesc.attributes[6].offset = sizeof(float) * 16;
    pipelineDesc.attributes[6].buffer = 1;
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    pipelineDesc.debugName = "instancing pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && instanceBuffer.valid() &&
                       uniformBuffer.valid() && pipeline.valid();
    if (!ready) log_error("instancing: resource creation failed");

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

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.5f, 300.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -90.0f)) *
                                Math::Mat4::RotationX(0.5f) * Math::Mat4::RotationY(time * 0.15f);
        const Math::Mat4 viewProjection = projection * view;

        fillInstances(&instances, time);

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &viewProjection, sizeof(Math::Mat4));
        driver->updateBuffer(instanceBuffer, 0, instances.data(),
                kInstanceCount * sizeof(Instance));
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(Math::Mat4));
        driver->drawIndexed(36, 0, kInstanceCount);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(instanceBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
