#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sprite.frag.h"
#include "sprite.geom.h"
#include "sprite.vert.h"

namespace
{

const unsigned kStarCount = 20000;
const float kGalaxyRadius = 5.0f;
const float kPi = 3.14159265f;

struct Star
{
    float position[3];
    float size;
    unsigned char color[4];
};

struct FrameUniforms
{
    Math::Mat4 view;
    Math::Mat4 projection;
    float params[4];
};

static_assert(sizeof(Star) == 20, "star size");

class Random
{
public:
    float next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 16777216.0f;
    }

private:
    unsigned state_ = 12345u;
};

unsigned char toByte(float value) { return static_cast<unsigned char>(value * 255.0f + 0.5f); }

void makeGalaxy(ct::Vector<Star>* stars)
{
    Random random;
    stars->resize(kStarCount);
    for (unsigned i = 0; i < kStarCount; ++i)
    {
        Star& star = (*stars)[i];
        const float unit = powf(random.next(), 1.6f);
        const float radius = unit * kGalaxyRadius;
        const float arm = static_cast<float>(i % 2) * kPi;
        const float spread = (random.next() - 0.5f) * (0.9f - 0.6f * unit);
        const float angle = arm + unit * 5.0f + spread * 2.0f;
        const float jitter = (random.next() + random.next() + random.next() - 1.5f) * 0.3f;
        star.position[0] = radius * cosf(angle);
        star.position[1] = jitter * expf(-unit * 2.0f);
        star.position[2] = radius * sinf(angle);
        star.size = 0.05f + 0.07f * random.next() + 0.05f * (1.0f - unit);
        const float warm = 1.0f - unit;
        star.color[0] = toByte(0.55f + 0.45f * warm);
        star.color[1] = toByte(0.65f + 0.20f * warm);
        star.color[2] = toByte(1.00f - 0.40f * warm);
        star.color[3] = 255;
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

    PlatformWindow* window = zenapp::openWindow("prisma 17 point sprites", driverType);
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

    if (!driver->caps().geometryShaders)
    {
        log_error("point sprites: this GPU has no geometry shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<Star> stars;
    makeGalaxy(&stars);

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = static_cast<std::uint32_t>(stars.size() * sizeof(Star));
    bufferDesc.data = stars.data();
    bufferDesc.debugName = "galaxy points";
    const prisma::BufferHandle starBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "sprite uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, sprite_vert);
    const prisma::ShaderHandle geometryShader = zenapp::createShader(driver, sprite_geom);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, sprite_frag);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.geometryShader = geometryShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.topology = prisma::Topology::Points;
    pipelineDesc.vertexBuffers[0].stride = sizeof(Star);
    pipelineDesc.vertexBufferCount = 1;
    pipelineDesc.attributeCount = 3;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float1;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.attributes[2].location = 2;
    pipelineDesc.attributes[2].format = prisma::VertexFormat::UByte4Norm;
    pipelineDesc.attributes[2].offset = sizeof(float) * 4;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.cullMode = prisma::CullMode::None;
    pipelineDesc.blend = true;
    pipelineDesc.srcColor = prisma::BlendFactor::One;
    pipelineDesc.dstColor = prisma::BlendFactor::One;
    pipelineDesc.srcAlpha = prisma::BlendFactor::One;
    pipelineDesc.dstAlpha = prisma::BlendFactor::One;
    pipelineDesc.debugName = "sprite pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(geometryShader);
    driver->destroy(fragmentShader);

    const bool ready = starBuffer.valid() && uniformBuffer.valid() && pipeline.valid();
    if (!ready) log_error("point sprites: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.01f;
    pass.clearColor[1] = 0.01f;
    pass.clearColor[2] = 0.03f;

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

        FrameUniforms frame;
        frame.projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        frame.view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -9.0f)) *
                     Math::Mat4::RotationX(0.9f);
        frame.params[0] = time;
        frame.params[1] = frame.params[2] = frame.params[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, starBuffer, 0);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->draw(kStarCount, 0);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(starBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
