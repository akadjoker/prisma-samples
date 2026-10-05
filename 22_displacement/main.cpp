#include "common/Projection.h"
#include "common/TextureLoader.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disp.frag.h"
#include "disp.tesc.h"
#include "disp.tese.h"
#include "disp.vert.h"
#include "disp_wire.frag.h"

namespace
{

const unsigned kGridSize = 16;
const float kPatchSize = 1.5f;
const float kTilesPerUnit = 0.25f;
const float kMaxLevel = 32.0f;
const float kTessellationFactor = 64.0f;
const float kFogDensity = 0.09f;

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    float camera[4];
    float lightDirection[4];
    float params[4];
    float fog[4];
};

void fillGrid(ct::Vector<float>* vertices)
{
    const float origin = -0.5f * kPatchSize * static_cast<float>(kGridSize);
    vertices->reserve(kGridSize * kGridSize * 8);
    for (unsigned row = 0; row < kGridSize; ++row)
    {
        for (unsigned column = 0; column < kGridSize; ++column)
        {
            const float x0 = origin + static_cast<float>(column) * kPatchSize;
            const float z0 = origin + static_cast<float>(row) * kPatchSize;
            const float x1 = x0 + kPatchSize;
            const float z1 = z0 + kPatchSize;
            const float corners[8] = { x0, z0, x1, z0, x1, z1, x0, z1 };
            for (unsigned i = 0; i < 8; ++i) vertices->push_back(corners[i]);
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

    PlatformWindow* window = zenapp::openWindow("prisma 22 displacement", driverType);
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

    if (!driver->caps().tessellation || driver->caps().maxPatchControlPoints < 4)
    {
        log_error("displacement: this GPU has no tessellation for 4 point patches");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<float> grid;
    fillGrid(&grid);
    const unsigned vertexCount = kGridSize * kGridSize * 4;

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Vertex;
    bufferDesc.size = static_cast<std::uint32_t>(grid.size() * sizeof(float));
    bufferDesc.data = grid.data();
    bufferDesc.debugName = "grid";
    const prisma::BufferHandle gridBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    char path[1024];
    zenapp::mediaPath("Textures/rocks.jpg", path, sizeof(path));
    const prisma::TextureHandle diffuse = zenapp::loadTexture(driver, path, true, true);
    if (!diffuse.valid()) log_error("displacement: cannot load %s", path);
    zenapp::mediaPath("Textures/rocks_NM_height.dds", path, sizeof(path));
    const prisma::TextureHandle normalHeight = zenapp::loadTexture(driver, path, false, true);
    if (!normalHeight.valid()) log_error("displacement: cannot load %s", path);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "surface sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, disp_vert);
    const prisma::ShaderHandle controlShader = zenapp::createShader(driver, disp_tesc);
    const prisma::ShaderHandle evaluationShader = zenapp::createShader(driver, disp_tese);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, disp_frag);
    const prisma::ShaderHandle wireShader = zenapp::createShader(driver, disp_wire_frag);

    prisma::PipelineDesc surfaceDesc;
    surfaceDesc.vertexShader = vertexShader;
    surfaceDesc.tessControlShader = controlShader;
    surfaceDesc.tessEvalShader = evaluationShader;
    surfaceDesc.fragmentShader = fragmentShader;
    surfaceDesc.topology = prisma::Topology::Patches;
    surfaceDesc.patchControlPoints = 4;
    surfaceDesc.vertexBuffers[0].stride = sizeof(float) * 2;
    surfaceDesc.vertexBufferCount = 1;
    surfaceDesc.attributeCount = 1;
    surfaceDesc.attributes[0].location = 0;
    surfaceDesc.attributes[0].format = prisma::VertexFormat::Float2;
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

    const bool ready = gridBuffer.valid() && uniformBuffer.valid() && diffuse.valid() &&
                       normalHeight.valid() && sampler.valid() && surfacePipeline.valid() &&
                       (!wireSupported || wirePipeline.valid());
    if (!ready) log_error("displacement: resource creation failed");

    const float fogColor[3] = { 0.55f, 0.62f, 0.72f };
    prisma::RenderPassDesc pass;
    pass.clearColor[0] = fogColor[0];
    pass.clearColor[1] = fogColor[1];
    pass.clearColor[2] = fogColor[2];

    float scale = 0.3f;
    bool wire = wireSupported && zenapp::hasArgument(argc, argv, "wire");
    const Math::Vec3 light = Math::Vec3(0.6f, 0.45f, 0.3f).Normalized();

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_UP) && scale < 1.0f)
        {
            scale += 0.05f;
            printf("displacement scale %.2f\n", scale);
        }
        if (key_pressed(window, KEY_DOWN) && scale > 0.01f)
        {
            scale -= 0.05f;
            if (scale < 0.0f) scale = 0.0f;
            printf("displacement scale %.2f\n", scale);
        }
        if (key_pressed(window, KEY_W) && wireSupported) wire = !wire;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const float angle = time * 0.12f;
        const Math::Vec3 eye(6.0f * cosf(angle), 1.5f + 0.3f * sinf(angle * 3.0f),
                6.0f * sinf(angle));
        const Math::Vec3 forward(-sinf(angle), 0.0f, cosf(angle));
        const Math::Vec3 target = eye + forward * 4.0f + Math::Vec3(0.0f, -1.6f, 0.0f);

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.05f, 100.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, target, Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.lightDirection[0] = light.x;
        frame.lightDirection[1] = light.y;
        frame.lightDirection[2] = light.z;
        frame.lightDirection[3] = 0.0f;
        frame.params[0] = scale;
        frame.params[1] = kTessellationFactor;
        frame.params[2] = kMaxLevel;
        frame.params[3] = kTilesPerUnit;
        frame.fog[0] = fogColor[0];
        frame.fog[1] = fogColor[1];
        frame.fog[2] = fogColor[2];
        frame.fog[3] = kFogDensity;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);
        driver->bindPipeline(surfacePipeline);
        driver->bindVertexBuffer(0, gridBuffer, 0);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, diffuse, sampler);
        driver->bindTexture(1, normalHeight, sampler);
        driver->draw(vertexCount, 0);
        if (wire)
        {
            driver->bindPipeline(wirePipeline);
            driver->bindVertexBuffer(0, gridBuffer, 0);
            driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
            driver->bindTexture(1, normalHeight, sampler);
            driver->draw(vertexCount, 0);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (wirePipeline.valid()) driver->destroy(wirePipeline);
    if (surfacePipeline.valid()) driver->destroy(surfacePipeline);
    if (sampler.valid()) driver->destroy(sampler);
    if (normalHeight.valid()) driver->destroy(normalHeight);
    if (diffuse.valid()) driver->destroy(diffuse);
    if (uniformBuffer.valid()) driver->destroy(uniformBuffer);
    if (gridBuffer.valid()) driver->destroy(gridBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
