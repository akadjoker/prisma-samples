#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid.frag.h"
#include "fluid.vert.h"
#include "fluid_density.comp.h"
#include "fluid_force.comp.h"
#include "fluid_step.comp.h"

namespace
{

const std::uint32_t kParticleCount = 8192;
const std::uint32_t kGroupSize = 128;
const unsigned kSubSteps = 2;
const float kStep = 1.0f / 120.0f;

const float kBoxWidth = 16.0f;
const float kBoxHeight = 9.0f;
const float kSpacing = 0.07f;
const float kRadius = 0.26f;
const float kStiffness = 350.0f;
const float kNearStiffness = 35.0f;
const float kViscosity = 0.03f;
const float kGravity = 9.8f;
const float kWallDamping = 0.3f;
const float kPi = 3.14159265f;

struct Particle
{
    float position[2];
    float velocity[2];
    float density[2];
    float acceleration[2];
};

struct SimulationParams
{
    float sim0[4];
    float sim1[4];
    float sim2[4];
    float sim3[4];
};

struct FrameUniforms
{
    float view[4];
    float params[4];
};

float randomUnit() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }

void fillParticles(ct::Vector<Particle>* particles)
{
    srand(77);
    particles->resize(kParticleCount);
    const unsigned rows = 112;
    for (std::uint32_t i = 0; i < kParticleCount; ++i)
    {
        const unsigned column = i / rows;
        const unsigned row = i % rows;
        Particle& particle = (*particles)[i];
        particle.position[0] = 0.1f + (static_cast<float>(column) + 0.5f) * kSpacing +
                               (randomUnit() - 0.5f) * kSpacing * 0.1f;
        particle.position[1] = 0.1f + (static_cast<float>(row) + 0.5f) * kSpacing +
                               (randomUnit() - 0.5f) * kSpacing * 0.1f;
        particle.velocity[0] = particle.velocity[1] = 0.0f;
        particle.density[0] = particle.density[1] = 1.0f;
        particle.acceleration[0] = particle.acceleration[1] = 0.0f;
    }
}

float latticeDensity()
{
    float sum = 0.0f;
    for (int y = -8; y <= 8; ++y)
    {
        for (int x = -8; x <= 8; ++x)
        {
            const float distance = kSpacing * sqrtf(static_cast<float>(x * x + y * y));
            if (distance < kRadius)
            {
                const float v = kRadius - distance;
                sum += v * v;
            }
        }
    }
    const float h2 = kRadius * kRadius;
    return sum * 6.0f / (kPi * h2 * h2);
}

void fillParams(SimulationParams* params, float restDensity, float time)
{
    const float stirX = 0.5f * kBoxWidth + 5.5f * sinf(0.45f * time);
    const float stirY = 1.2f + 0.7f * sinf(0.8f * time + 1.0f);
    const float stirVx = 5.5f * 0.45f * cosf(0.45f * time);
    const float stirVy = 0.7f * 0.8f * cosf(0.8f * time + 1.0f);
    params->sim0[0] = kStep;
    params->sim0[1] = kRadius;
    params->sim0[2] = restDensity;
    params->sim0[3] = kStiffness;
    params->sim1[0] = kNearStiffness;
    params->sim1[1] = kViscosity;
    params->sim1[2] = kGravity;
    params->sim1[3] = static_cast<float>(kParticleCount);
    params->sim2[0] = kBoxWidth;
    params->sim2[1] = kBoxHeight;
    params->sim2[2] = stirX;
    params->sim2[3] = stirY;
    params->sim3[0] = 0.8f;
    params->sim3[1] = stirVx;
    params->sim3[2] = stirVy;
    params->sim3[3] = kWallDamping;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 23 fluid", driverType);
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

    if (!driver->caps().compute || !driver->caps().storageBuffersInGraphics)
    {
        log_error("fluid: this GPU has no compute shaders or no storage buffers in vertex shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<Particle> particles;
    fillParticles(&particles);
    const float restDensity = latticeDensity();

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = kParticleCount * sizeof(Particle);
    bufferDesc.data = particles.data();
    bufferDesc.debugName = "particles";
    const prisma::BufferHandle particleBuffer = driver->createBuffer(bufferDesc);

    const std::uint32_t alignment = driver->caps().uniformBufferOffsetAlignment;
    const std::uint32_t paramsStride =
            (static_cast<std::uint32_t>(sizeof(SimulationParams)) + alignment - 1) / alignment *
            alignment;

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = paramsStride * kSubSteps;
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "simulation params";
    const prisma::BufferHandle paramsBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle densityShader = zenapp::createShader(driver, fluid_density_comp);
    const prisma::ShaderHandle forceShader = zenapp::createShader(driver, fluid_force_comp);
    const prisma::ShaderHandle stepShader = zenapp::createShader(driver, fluid_step_comp);
    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, fluid_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, fluid_frag);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.uniformBlockCount = 1;
    computeDesc.uniformBlocks[0].name = "Params";
    computeDesc.uniformBlocks[0].slot = 1;
    computeDesc.storageBufferCount = 1;
    computeDesc.storageBuffers[0].name = "Particles";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.shader = densityShader;
    computeDesc.debugName = "fluid density";
    const prisma::PipelineHandle densityPipeline = driver->createComputePipeline(computeDesc);
    computeDesc.shader = forceShader;
    computeDesc.debugName = "fluid force";
    const prisma::PipelineHandle forcePipeline = driver->createComputePipeline(computeDesc);
    computeDesc.shader = stepShader;
    computeDesc.debugName = "fluid step";
    const prisma::PipelineHandle stepPipeline = driver->createComputePipeline(computeDesc);

    prisma::PipelineDesc drawDesc;
    drawDesc.vertexShader = vertexShader;
    drawDesc.fragmentShader = fragmentShader;
    drawDesc.topology = prisma::Topology::TriangleStrip;
    drawDesc.storageBufferCount = 1;
    drawDesc.storageBuffers[0].name = "Particles";
    drawDesc.storageBuffers[0].slot = 0;
    drawDesc.depthTest = false;
    drawDesc.depthWrite = false;
    drawDesc.blend = true;
    drawDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    drawDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    drawDesc.srcAlpha = prisma::BlendFactor::One;
    drawDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    drawDesc.debugName = "fluid draw";
    const prisma::PipelineHandle drawPipeline = driver->createPipeline(drawDesc);

    driver->destroy(densityShader);
    driver->destroy(forceShader);
    driver->destroy(stepShader);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = particleBuffer.valid() && paramsBuffer.valid() && frameBuffer.valid() &&
                       densityPipeline.valid() && forcePipeline.valid() && stepPipeline.valid() &&
                       drawPipeline.valid();
    if (!ready) log_error("fluid: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.03f;
    pass.clearColor[1] = 0.04f;
    pass.clearColor[2] = 0.07f;

    const std::uint32_t groups = kParticleCount / kGroupSize;
    unsigned long long simulationStep = 0;
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

        float scaleY = 2.0f / kBoxHeight;
        float scaleX = scaleY / aspect;
        if (scaleX * kBoxWidth > 2.0f)
        {
            scaleX = 2.0f / kBoxWidth;
            scaleY = scaleX * aspect;
        }
        FrameUniforms frame;
        frame.view[0] = scaleX;
        frame.view[1] = scaleY;
        frame.view[2] = -0.5f * kBoxWidth * scaleX;
        frame.view[3] = -0.5f * kBoxHeight * scaleY;
        frame.params[0] = 0.12f;
        frame.params[1] = 0.12f;
        frame.params[2] = restDensity;
        frame.params[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        for (unsigned s = 0; s < kSubSteps; ++s)
        {
            SimulationParams params;
            fillParams(&params, restDensity, static_cast<float>(simulationStep + s) * kStep);
            driver->updateBuffer(paramsBuffer, s * paramsStride, &params, sizeof(params));
        }
        simulationStep += kSubSteps;

        driver->beginComputePass();
        for (unsigned s = 0; s < kSubSteps; ++s)
        {
            const prisma::PipelineHandle pipelines[3] = { densityPipeline, forcePipeline,
                stepPipeline };
            for (unsigned p = 0; p < 3; ++p)
            {
                driver->bindPipeline(pipelines[p]);
                driver->bindUniformBuffer(1, paramsBuffer, s * paramsStride,
                        sizeof(SimulationParams));
                driver->bindStorageBuffer(0, particleBuffer, 0, kParticleCount * sizeof(Particle));
                driver->dispatch(groups, 1, 1);
            }
        }
        driver->endComputePass();

        driver->beginRenderPass(pass);
        driver->bindPipeline(drawPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindStorageBuffer(0, particleBuffer, 0, kParticleCount * sizeof(Particle));
        driver->draw(4, 0, kParticleCount);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (drawPipeline.valid()) driver->destroy(drawPipeline);
    if (stepPipeline.valid()) driver->destroy(stepPipeline);
    if (forcePipeline.valid()) driver->destroy(forcePipeline);
    if (densityPipeline.valid()) driver->destroy(densityPipeline);
    if (frameBuffer.valid()) driver->destroy(frameBuffer);
    if (paramsBuffer.valid()) driver->destroy(paramsBuffer);
    if (particleBuffer.valid()) driver->destroy(particleBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
