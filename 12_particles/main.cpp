#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "particles.comp.h"
#include "particles.frag.h"
#include "particles.vert.h"

namespace
{

const std::uint32_t kParticleCount = 16384;
const std::uint32_t kGroupSize = 64;

struct Particle
{
    float position[4];
    float velocity[4];
};

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    float params[4];
};

float randomUnit() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }

void fillParticles(ct::Vector<Particle>* particles)
{
    srand(1234);
    particles->resize(kParticleCount);
    for (std::uint32_t i = 0; i < kParticleCount; ++i)
    {
        const float radius = 0.25f + 1.6f * sqrtf(randomUnit());
        const float angle = randomUnit() * 6.2831853f;
        const Math::Vec3 position(radius * cosf(angle), (randomUnit() - 0.5f) * 0.15f,
                radius * sinf(angle));
        const float speed = 1.2247449f * radius / powf(radius * radius + 0.04f, 0.75f);
        const Math::Vec3 tangent(sinf(angle), 0.0f, -cosf(angle));
        const Math::Vec3 velocity = tangent * (speed * (0.9f + 0.2f * randomUnit()));

        Particle& particle = (*particles)[i];
        particle.position[0] = position.x;
        particle.position[1] = position.y;
        particle.position[2] = position.z;
        particle.position[3] = 1.0f;
        particle.velocity[0] = velocity.x;
        particle.velocity[1] = velocity.y;
        particle.velocity[2] = velocity.z;
        particle.velocity[3] = 0.0f;
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

    PlatformWindow* window = zenapp::openWindow("prisma 12 particles", driverType);
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
        log_error("particles: this GPU has no compute shaders or no storage buffers in vertex "
                  "shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<Particle> particles;
    fillParticles(&particles);

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = kParticleCount * sizeof(Particle);
    bufferDesc.data = particles.data();
    bufferDesc.debugName = "particles";
    const prisma::BufferHandle particleBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(float) * 4;
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "simulation params";
    const prisma::BufferHandle paramsBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle computeShader = zenapp::createShader(driver, particles_comp);
    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, particles_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, particles_frag);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.shader = computeShader;
    computeDesc.uniformBlockCount = 1;
    computeDesc.uniformBlocks[0].name = "Params";
    computeDesc.uniformBlocks[0].slot = 1;
    computeDesc.storageBufferCount = 1;
    computeDesc.storageBuffers[0].name = "Particles";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.debugName = "particles compute";
    const prisma::PipelineHandle computePipeline = driver->createComputePipeline(computeDesc);

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
    drawDesc.srcColor = prisma::BlendFactor::One;
    drawDesc.dstColor = prisma::BlendFactor::One;
    drawDesc.srcAlpha = prisma::BlendFactor::One;
    drawDesc.dstAlpha = prisma::BlendFactor::One;
    drawDesc.debugName = "particles draw";
    const prisma::PipelineHandle drawPipeline = driver->createPipeline(drawDesc);

    driver->destroy(computeShader);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = particleBuffer.valid() && paramsBuffer.valid() && frameBuffer.valid() &&
                       computePipeline.valid() && drawPipeline.valid();
    if (!ready) log_error("particles: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.02f;

    double previous = time_seconds();
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

        const double now = time_seconds();
        float dt = static_cast<float>(now - previous);
        previous = now;
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
        if (still) dt = 1.0f / 60.0f;
        const float time = (still ? 0.0f : static_cast<float>(now)) + 0.6f;

        const float simulation[4] = { dt, static_cast<float>(kParticleCount), 0.0f, 0.0f };

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -4.0f)) *
                                Math::Mat4::RotationX(0.5f) * Math::Mat4::RotationY(time * 0.2f);
        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.params[0] = 0.012f / aspect;
        frame.params[1] = 0.012f;
        frame.params[2] = 0.1f;
        frame.params[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(paramsBuffer, 0, simulation, sizeof(simulation));
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));

        driver->beginComputePass();
        driver->bindPipeline(computePipeline);
        driver->bindUniformBuffer(1, paramsBuffer, 0, sizeof(simulation));
        driver->bindStorageBuffer(0, particleBuffer, 0, kParticleCount * sizeof(Particle));
        driver->dispatch(kParticleCount / kGroupSize, 1, 1);
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

    driver->destroy(drawPipeline);
    driver->destroy(computePipeline);
    driver->destroy(frameBuffer);
    driver->destroy(paramsBuffer);
    driver->destroy(particleBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
