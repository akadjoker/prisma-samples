#include "common/Projection.h"
#include "common/TextureLoader.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nbody.comp.h"
#include "nbody.frag.h"
#include "nbody.vert.h"

namespace
{

const std::uint32_t kBodyCount = 8192;
const std::uint32_t kGroupSize = 128;
const float kStep = 1.0f / 60.0f;
const float kSoftening = 0.01f;
const float kGravity = 1.0f;

struct Body
{
    float position[4];
    float velocity[4];
};

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    float size[4];
    float options[4];
};

struct Galaxy
{
    Math::Vec3 center;
    Math::Vec3 velocity;
    float tiltX;
    float tiltZ;
    float radius;
};

const Galaxy kGalaxies[3] = {
    { Math::Vec3(-3.4f, 0.0f, 0.5f), Math::Vec3(0.12f, 0.0f, 0.0f), 0.45f, 0.1f, 1.0f },
    { Math::Vec3(2.9f, -0.2f, 2.0f), Math::Vec3(-0.10f, 0.0f, -0.08f), -0.55f, 0.3f, 0.9f },
    { Math::Vec3(0.0f, 0.4f, -3.6f), Math::Vec3(0.0f, 0.0f, 0.14f), 0.8f, -0.2f, 0.8f },
};

float randomUnit() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }

void fillBodies(ct::Vector<Body>* bodies)
{
    srand(2024);
    bodies->resize(kBodyCount);
    const std::uint32_t perGalaxy = kBodyCount / 3;
    for (std::uint32_t i = 0; i < kBodyCount; ++i)
    {
        std::uint32_t index = i / perGalaxy;
        if (index > 2) index = 2;
        const Galaxy& galaxy = kGalaxies[index];
        const std::uint32_t count = index == 2 ? kBodyCount - 2 * perGalaxy : perGalaxy;

        const Math::Mat4 tilt =
                Math::Mat4::RotationZ(galaxy.tiltZ) * Math::Mat4::RotationX(galaxy.tiltX);
        const Math::Vec3 axisU = (tilt * Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f)).xyz();
        const Math::Vec3 axisV = (tilt * Math::Vec4(0.0f, 0.0f, 1.0f, 0.0f)).xyz();
        const Math::Vec3 normal = (tilt * Math::Vec4(0.0f, 1.0f, 0.0f, 0.0f)).xyz();

        const float unit = randomUnit();
        const float radius = galaxy.radius * powf(unit, 1.5f) + 0.02f;
        const float angle = randomUnit() * 6.2831853f;
        const float height = (randomUnit() + randomUnit() - 1.0f) * 0.06f * radius;
        const float c = cosf(angle);
        const float s = sinf(angle);
        const Math::Vec3 position =
                galaxy.center + axisU * (radius * c) + axisV * (radius * s) + normal * height;

        const float mass = 1.0f / static_cast<float>(count);
        const float enclosed = powf(radius / galaxy.radius, 2.0f / 3.0f);
        const float speedSquared =
                kGravity * enclosed * radius * radius / powf(radius * radius + kSoftening, 1.5f);
        const float speed = sqrtf(speedSquared) * (0.97f + 0.06f * randomUnit());
        const Math::Vec3 scatter(randomUnit() - 0.5f, randomUnit() - 0.5f, randomUnit() - 0.5f);
        const Math::Vec3 velocity =
                galaxy.velocity + (axisV * c - axisU * s) * speed + scatter * (0.25f * speed);

        Body& body = (*bodies)[i];
        body.position[0] = position.x;
        body.position[1] = position.y;
        body.position[2] = position.z;
        body.position[3] = mass;
        body.velocity[0] = velocity.x;
        body.velocity[1] = velocity.y;
        body.velocity[2] = velocity.z;
        body.velocity[3] = 0.0f;
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

    PlatformWindow* window = zenapp::openWindow("prisma 24 n-body", driverType);
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
        log_error("n-body: this GPU has no compute shaders or no storage buffers in vertex "
                  "shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<Body> bodies;
    fillBodies(&bodies);

    const std::uint32_t bodyBytes = kBodyCount * sizeof(Body);
    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = bodyBytes;
    bufferDesc.data = bodies.data();
    bufferDesc.debugName = "bodies a";
    prisma::BufferHandle bodyBuffers[2];
    bodyBuffers[0] = driver->createBuffer(bufferDesc);
    bufferDesc.debugName = "bodies b";
    bodyBuffers[1] = driver->createBuffer(bufferDesc);

    const float simulation[4] = { kStep, static_cast<float>(kBodyCount), kSoftening, kGravity };
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(simulation);
    bufferDesc.data = simulation;
    bufferDesc.debugName = "simulation params";
    const prisma::BufferHandle paramsBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);

    char path[1024];
    zenapp::mediaPath("misc/Particle.dds", path, sizeof(path));
    prisma::TextureHandle sprite = zenapp::loadTexture(driver, path, false, false);
    const bool textured = sprite.valid();
    if (!textured)
    {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        prisma::TextureDesc spriteDesc;
        spriteDesc.format = prisma::TextureFormat::RGBA8;
        spriteDesc.width = 1;
        spriteDesc.height = 1;
        spriteDesc.data = white;
        spriteDesc.debugName = "fallback sprite";
        sprite = driver->createTexture(spriteDesc);
    }

    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = prisma::Filter::Linear;
    samplerDesc.magFilter = prisma::Filter::Linear;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "sprite sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const prisma::ShaderHandle computeShader = zenapp::createShader(driver, nbody_comp);
    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, nbody_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, nbody_frag);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.shader = computeShader;
    computeDesc.uniformBlockCount = 1;
    computeDesc.uniformBlocks[0].name = "Params";
    computeDesc.uniformBlocks[0].slot = 0;
    computeDesc.storageBufferCount = 2;
    computeDesc.storageBuffers[0].name = "Source";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.storageBuffers[1].name = "Target";
    computeDesc.storageBuffers[1].slot = 1;
    computeDesc.debugName = "n-body compute";
    const prisma::PipelineHandle computePipeline = driver->createComputePipeline(computeDesc);

    prisma::PipelineDesc drawDesc;
    drawDesc.vertexShader = vertexShader;
    drawDesc.fragmentShader = fragmentShader;
    drawDesc.topology = prisma::Topology::TriangleStrip;
    drawDesc.storageBufferCount = 1;
    drawDesc.storageBuffers[0].name = "Bodies";
    drawDesc.storageBuffers[0].slot = 0;
    drawDesc.depthTest = false;
    drawDesc.depthWrite = false;
    drawDesc.blend = true;
    drawDesc.srcColor = prisma::BlendFactor::One;
    drawDesc.dstColor = prisma::BlendFactor::One;
    drawDesc.srcAlpha = prisma::BlendFactor::One;
    drawDesc.dstAlpha = prisma::BlendFactor::One;
    drawDesc.debugName = "n-body draw";
    const prisma::PipelineHandle drawPipeline = driver->createPipeline(drawDesc);

    driver->destroy(computeShader);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = bodyBuffers[0].valid() && bodyBuffers[1].valid() && paramsBuffer.valid() &&
                       frameBuffer.valid() && sprite.valid() && sampler.valid() &&
                       computePipeline.valid() && drawPipeline.valid();
    if (!ready) log_error("n-body: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.015f;

    double previous = time_seconds();
    float accumulator = 0.0f;
    float simulated = 0.0f;
    int current = 0;
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);

        const double now = time_seconds();
        float dt = static_cast<float>(now - previous);
        previous = now;
        if (dt > 0.1f) dt = 0.1f;
        accumulator += dt;
        int steps = 0;
        if (still)
        {
            steps = 1;
        }
        else
        {
            while (accumulator >= kStep && steps < 3)
            {
                accumulator -= kStep;
                ++steps;
            }
            if (steps == 3) accumulator = 0.0f;
        }
        simulated += static_cast<float>(steps) * kStep;

        const float angle = still ? 0.6f : 0.6f + simulated * 0.08f;
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.9f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -8.5f)) *
                                Math::Mat4::RotationX(0.45f) * Math::Mat4::RotationY(angle);
        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.size[0] = 0.014f / aspect;
        frame.size[1] = 0.014f;
        frame.size[2] = frame.size[3] = 0.0f;
        frame.options[0] = 0.28f;
        frame.options[1] = 0.6f;
        frame.options[2] = textured ? 1.0f : 0.0f;
        frame.options[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));

        for (int step = 0; step < steps; ++step)
        {
            driver->beginComputePass();
            driver->bindPipeline(computePipeline);
            driver->bindUniformBuffer(0, paramsBuffer, 0, sizeof(simulation));
            driver->bindStorageBuffer(0, bodyBuffers[current], 0, bodyBytes);
            driver->bindStorageBuffer(1, bodyBuffers[1 - current], 0, bodyBytes);
            driver->dispatch(kBodyCount / kGroupSize, 1, 1);
            driver->endComputePass();
            current = 1 - current;
        }

        driver->beginRenderPass(pass);
        driver->bindPipeline(drawPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, sprite, sampler);
        driver->bindStorageBuffer(0, bodyBuffers[current], 0, bodyBytes);
        driver->draw(4, 0, kBodyCount);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(drawPipeline);
    driver->destroy(computePipeline);
    driver->destroy(sampler);
    driver->destroy(sprite);
    driver->destroy(frameBuffer);
    driver->destroy(paramsBuffer);
    driver->destroy(bodyBuffers[1]);
    driver->destroy(bodyBuffers[0]);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
