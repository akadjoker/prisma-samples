#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "patch.frag.h"
#include "patch.tesc.h"
#include "patch.tese.h"
#include "patch.vert.h"
#include "wire.frag.h"

namespace
{

const unsigned kPointCount = 16;
const int kMinLevel = 1;
const int kMaxLevel = 32;

struct SurfaceUniforms
{
    Math::Mat4 viewProjection;
    float level[4];
    float lightDirection[4];
};

void fillControlPoints(float time, float (*points)[3])
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float* point = points[row * 4 + column];
            const float phase = time * 1.3f + static_cast<float>(column) * 1.1f +
                                static_cast<float>(row) * 0.7f;
            point[0] = (static_cast<float>(column) - 1.5f) * 1.4f;
            point[1] = 0.8f * sinf(phase);
            point[2] = (static_cast<float>(row) - 1.5f) * 1.4f;
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

    PlatformWindow* window = zenapp::openWindow("prisma 16 tessellation", driverType);
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

    if (!driver->caps().tessellation || driver->caps().maxPatchControlPoints < kPointCount)
    {
        log_error("tessellation: this GPU has no tessellation for 16 point patches");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    float points[kPointCount][3];
    fillControlPoints(0.0f, points);

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(points);
    bufferDesc.data = points;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "patch control points";
    const prisma::BufferHandle pointBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(SurfaceUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "surface uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, patch_vert);
    const prisma::ShaderHandle controlShader = zenapp::createShader(driver, patch_tesc);
    const prisma::ShaderHandle evaluationShader = zenapp::createShader(driver, patch_tese);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, patch_frag);
    const prisma::ShaderHandle wireShader = zenapp::createShader(driver, wire_frag);

    prisma::PipelineDesc surfaceDesc;
    surfaceDesc.vertexShader = vertexShader;
    surfaceDesc.tessControlShader = controlShader;
    surfaceDesc.tessEvalShader = evaluationShader;
    surfaceDesc.fragmentShader = fragmentShader;
    surfaceDesc.topology = prisma::Topology::Patches;
    surfaceDesc.patchControlPoints = kPointCount;
    surfaceDesc.vertexBuffers[0].stride = sizeof(float) * 3;
    surfaceDesc.vertexBufferCount = 1;
    surfaceDesc.attributeCount = 1;
    surfaceDesc.attributes[0].location = 0;
    surfaceDesc.attributes[0].format = prisma::VertexFormat::Float3;
    surfaceDesc.attributes[0].offset = 0;
    surfaceDesc.depthTest = true;
    surfaceDesc.cullMode = prisma::CullMode::None;
    surfaceDesc.depthBiasConstant = 2.0f;
    surfaceDesc.depthBiasSlope = 1.5f;
    surfaceDesc.debugName = "surface pipeline";
    const prisma::PipelineHandle surfacePipeline = driver->createPipeline(surfaceDesc);

    const bool wireSupported = driver->caps().wireframe;
    prisma::PipelineHandle wirePipeline;
    if (wireSupported)
    {
        prisma::PipelineDesc wireDesc = surfaceDesc;
        wireDesc.fragmentShader = wireShader;
        wireDesc.wireframe = true;
        wireDesc.depthCompare = prisma::CompareOp::LessEqual;
        wireDesc.depthBiasConstant = 0.0f;
        wireDesc.depthBiasSlope = 0.0f;
        wireDesc.debugName = "wire pipeline";
        wirePipeline = driver->createPipeline(wireDesc);
    }

    driver->destroy(vertexShader);
    driver->destroy(controlShader);
    driver->destroy(evaluationShader);
    driver->destroy(fragmentShader);
    driver->destroy(wireShader);

    const bool ready = pointBuffer.valid() && uniformBuffer.valid() && surfacePipeline.valid() &&
                       (!wireSupported || wirePipeline.valid());
    if (!ready) log_error("tessellation: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int level = 8;
    bool wire = wireSupported;
    const Math::Vec3 light = Math::Vec3(0.4f, 0.8f, 0.5f).Normalized();

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_UP) && level < kMaxLevel)
        {
            ++level;
            printf("tessellation level %d\n", level);
        }
        if (key_pressed(window, KEY_DOWN) && level > kMinLevel)
        {
            --level;
            printf("tessellation level %d\n", level);
        }
        if (key_pressed(window, KEY_W) && wireSupported) wire = !wire;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        fillControlPoints(time, points);

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -6.5f)) *
                                Math::Mat4::RotationX(0.6f) * Math::Mat4::RotationY(time * 0.2f);

        SurfaceUniforms surface;
        surface.viewProjection = projection * view;
        surface.level[0] = static_cast<float>(level);
        surface.level[1] = surface.level[2] = surface.level[3] = 0.0f;
        surface.lightDirection[0] = light.x;
        surface.lightDirection[1] = light.y;
        surface.lightDirection[2] = light.z;
        surface.lightDirection[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(pointBuffer, 0, points, sizeof(points));
        driver->updateBuffer(uniformBuffer, 0, &surface, sizeof(surface));
        driver->beginRenderPass(pass);
        driver->bindPipeline(surfacePipeline);
        driver->bindVertexBuffer(0, pointBuffer, 0);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(SurfaceUniforms));
        driver->draw(kPointCount, 0);
        if (wire)
        {
            driver->bindPipeline(wirePipeline);
            driver->bindVertexBuffer(0, pointBuffer, 0);
            driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(SurfaceUniforms));
            driver->draw(kPointCount, 0);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (wirePipeline.valid()) driver->destroy(wirePipeline);
    driver->destroy(surfacePipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(pointBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
