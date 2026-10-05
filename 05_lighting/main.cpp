#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lighting.frag.h"
#include "lighting.vert.h"
#include "marker.frag.h"
#include "marker.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float normal[3];
};

struct SceneUniforms
{
    Math::Mat4 modelViewProjection;
    Math::Mat4 model;
    float lightDirection[2][4];
    float lightColor[2][4];
    float ambient[4];
};

struct MarkerUniforms
{
    Math::Mat4 modelViewProjection;
    float color[4];
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

const float kLightColors[2][3] = { { 1.0f, 0.85f, 0.6f }, { 0.3f, 0.5f, 1.0f } };

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

    PlatformWindow* window = zenapp::openWindow("prisma 05 lighting", driverType);
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

    const std::uint32_t kRangeCount = 3;
    const std::uint32_t stride =
            alignUp(sizeof(SceneUniforms), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "lighting uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle litVertex = zenapp::createShader(driver, lighting_vert);
    const prisma::ShaderHandle litFragment = zenapp::createShader(driver, lighting_frag);
    const prisma::ShaderHandle markerVertex = zenapp::createShader(driver, marker_vert);
    const prisma::ShaderHandle markerFragment = zenapp::createShader(driver, marker_frag);

    prisma::PipelineDesc litDesc;
    litDesc.vertexShader = litVertex;
    litDesc.fragmentShader = litFragment;
    litDesc.vertexBuffers[0].stride = sizeof(Vertex);
    litDesc.vertexBufferCount = 1;
    litDesc.attributeCount = 2;
    litDesc.attributes[0].location = 0;
    litDesc.attributes[0].format = prisma::VertexFormat::Float3;
    litDesc.attributes[0].offset = 0;
    litDesc.attributes[1].location = 1;
    litDesc.attributes[1].format = prisma::VertexFormat::Float3;
    litDesc.attributes[1].offset = sizeof(float) * 3;
    litDesc.depthTest = true;
    litDesc.cullMode = prisma::CullMode::Back;
    litDesc.debugName = "lit pipeline";
    const prisma::PipelineHandle litPipeline = driver->createPipeline(litDesc);

    prisma::PipelineDesc markerDesc = litDesc;
    markerDesc.vertexShader = markerVertex;
    markerDesc.fragmentShader = markerFragment;
    markerDesc.attributeCount = 1;
    markerDesc.debugName = "marker pipeline";
    const prisma::PipelineHandle markerPipeline = driver->createPipeline(markerDesc);

    driver->destroy(litVertex);
    driver->destroy(litFragment);
    driver->destroy(markerVertex);
    driver->destroy(markerFragment);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && uniformBuffer.valid() &&
                       litPipeline.valid() && markerPipeline.valid();
    if (!ready) log_error("lighting: resource creation failed");

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
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -8.0f)) *
                                Math::Mat4::RotationX(0.4f);
        const Math::Mat4 viewProjection = projection * view;

        Math::Vec3 lightDirection[2];
        lightDirection[0] = Math::Vec3(cosf(time * 0.8f), 0.7f, sinf(time * 0.8f)).Normalized();
        lightDirection[1] = Math::Vec3(-sinf(time * 0.5f), -0.3f, cosf(time * 0.5f)).Normalized();

        SceneUniforms scene;
        scene.model = Math::Mat4::RotationY(time) * Math::Mat4::RotationX(time * 0.7f);
        scene.modelViewProjection = viewProjection * scene.model;
        for (int i = 0; i < 2; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                scene.lightDirection[i][c] = lightDirection[i][c];
                scene.lightColor[i][c] = kLightColors[i][c];
            }
            scene.lightDirection[i][3] = 0.0f;
            scene.lightColor[i][3] = 1.0f;
        }
        scene.ambient[0] = scene.ambient[1] = scene.ambient[2] = 0.08f;
        scene.ambient[3] = 1.0f;
        memcpy(uniforms.data(), &scene, sizeof(scene));

        for (int i = 0; i < 2; ++i)
        {
            MarkerUniforms marker;
            marker.modelViewProjection = viewProjection *
                                         Math::Mat4::Translation(lightDirection[i] * 3.5f) *
                                         Math::Mat4::Scale(Math::Vec3(0.2f, 0.2f, 0.2f));
            for (int c = 0; c < 3; ++c) marker.color[c] = kLightColors[i][c];
            marker.color[3] = 1.0f;
            memcpy(uniforms.data() + (i + 1) * stride, &marker, sizeof(marker));
        }

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kRangeCount);
        driver->beginRenderPass(pass);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindPipeline(litPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(SceneUniforms));
        driver->drawIndexed(36, 0);
        driver->bindPipeline(markerPipeline);
        for (std::uint32_t i = 1; i < kRangeCount; ++i)
        {
            driver->bindUniformBuffer(0, uniformBuffer, i * stride, sizeof(MarkerUniforms));
            driver->drawIndexed(36, 0);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(markerPipeline);
    driver->destroy(litPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
