#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ball.frag.h"
#include "ball.vert.h"
#include "blur.frag.h"
#include "bright.frag.h"
#include "final.frag.h"
#include "fullscreen.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"

namespace
{

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 inverseViewProjection;
    Math::Mat4 model;
    float cameraPosition[4];
};

struct PostUniforms
{
    float params[4];
};

enum Range : std::uint32_t
{
    kRangeFrame,
    kRangeBright,
    kRangeBlurHorizontal,
    kRangeBlurVertical,
    kRangeFinal,
    kRangeCount
};

const std::uint32_t kRenderWidth = 1280;
const std::uint32_t kRenderHeight = 720;

struct Targets
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t halfWidth = 0;
    std::uint32_t halfHeight = 0;
    prisma::TextureHandle sceneColor;
    prisma::TextureHandle sceneDepth;
    prisma::TextureHandle bloomA;
    prisma::TextureHandle bloomB;

    bool valid() const
    {
        return sceneColor.valid() && sceneDepth.valid() && bloomA.valid() && bloomB.valid();
    }
};

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

prisma::TextureHandle createTarget(prisma::Driver* driver, prisma::TextureFormat format,
        std::uint32_t width, std::uint32_t height, std::uint32_t usage, const char* name)
{
    prisma::TextureDesc desc;
    desc.format = format;
    desc.width = width;
    desc.height = height;
    desc.usage = usage;
    desc.debugName = name;
    return driver->createTexture(desc);
}

void createTargets(prisma::Driver* driver, std::uint32_t width, std::uint32_t height,
        Targets* targets)
{
    const std::uint32_t both = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    targets->width = width;
    targets->height = height;
    targets->halfWidth = width / 2 > 0 ? width / 2 : 1;
    targets->halfHeight = height / 2 > 0 ? height / 2 : 1;
    targets->sceneColor = createTarget(driver, prisma::TextureFormat::RGBA16F, width, height, both,
            "scene color");
    targets->sceneDepth = createTarget(driver, prisma::TextureFormat::Depth32F, width, height,
            prisma::kTextureRenderTarget, "scene depth");
    targets->bloomA = createTarget(driver, prisma::TextureFormat::RGBA16F, targets->halfWidth,
            targets->halfHeight, both, "bloom a");
    targets->bloomB = createTarget(driver, prisma::TextureFormat::RGBA16F, targets->halfWidth,
            targets->halfHeight, both, "bloom b");
}

void destroyTargets(prisma::Driver* driver, Targets* targets)
{
    driver->destroy(targets->bloomB);
    driver->destroy(targets->bloomA);
    driver->destroy(targets->sceneDepth);
    driver->destroy(targets->sceneColor);
    *targets = Targets();
}

void measure(const zenapp::SdkMesh& mesh, Math::Vec3* center, float* size)
{
    const zenapp::SdkMeshData& data = mesh.data();
    const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[data.meshes[0].vertexBuffers[0]];
    unsigned offset = 0;
    for (unsigned e = 0; e < 32 && buffer.decl[e].stream != 0xFF; ++e)
        if (buffer.decl[e].usage == 0) offset = buffer.decl[e].offset;

    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (uint64_t i = 0; i < buffer.numVertices; ++i)
    {
        float position[3];
        memcpy(position, data.file.data() + buffer.dataOffset + i * buffer.strideBytes + offset,
                sizeof(position));
        low = Math::Vec3::Min(low, Math::Vec3(position[0], position[1], position[2]));
        high = Math::Vec3::Max(high, Math::Vec3(position[0], position[1], position[2]));
    }
    const Math::Vec3 half = (high - low) * 0.5f;
    *center = (low + high) * 0.5f;
    *size = half.x > half.y ? (half.x > half.z ? half.x : half.z)
                            : (half.y > half.z ? half.y : half.z);
}

void setTargetFormats(prisma::PipelineDesc* desc, bool depth)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = prisma::TextureFormat::RGBA16F;
    desc->targets.depth = depth ? prisma::TextureFormat::Depth32F : prisma::TextureFormat::None;
}

prisma::RenderPassDesc postPass(prisma::TextureHandle target)
{
    prisma::RenderPassDesc pass;
    pass.colors[0].texture = target;
    pass.colorCount = 1;
    pass.depthLoad = prisma::LoadOp::DontCare;
    pass.stencilLoad = prisma::LoadOp::DontCare;
    return pass;
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

    PlatformWindow* window = zenapp::openWindow("prisma 13 hdr", driverType);
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

    if (!driver->caps().floatColorTargets)
    {
        log_error("hdr: this GPU cannot render to float colour targets");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("Light Probes/uffizi_cross.dds", path, sizeof(path));
    const prisma::TextureHandle environment = zenapp::loadTexture(driver, path, false, false);
    if (!environment.valid())
    {
        log_error("hdr: cannot create %s on this GPU", path);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    zenapp::mediaPath("misc/ball.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh mesh;
    bool ready = mesh.load(driver, path, false) && mesh.meshCount() > 0;
    if (!ready) log_error("hdr: cannot load %s", path);

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    samplerDesc.magFilter = samplerDesc.minFilter;
    samplerDesc.mipFilter = linear ? prisma::MipFilter::Linear : prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressW = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "hdr sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const std::uint32_t stride =
            alignUp(sizeof(FrameUniforms), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "hdr uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle fullscreenVertex = zenapp::createShader(driver, fullscreen_vert);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);
    const prisma::ShaderHandle ballVertex = zenapp::createShader(driver, ball_vert);
    const prisma::ShaderHandle ballFragment = zenapp::createShader(driver, ball_frag);
    const prisma::ShaderHandle brightFragment = zenapp::createShader(driver, bright_frag);
    const prisma::ShaderHandle blurFragment = zenapp::createShader(driver, blur_frag);
    const prisma::ShaderHandle finalFragment = zenapp::createShader(driver, final_frag);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    setTargetFormats(&skyDesc, true);
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    prisma::PipelineDesc ballDesc;
    ballDesc.vertexShader = ballVertex;
    ballDesc.fragmentShader = ballFragment;
    if (ready) ready = mesh.layout(0, &ballDesc);
    ballDesc.depthTest = true;
    ballDesc.cullMode = prisma::CullMode::Back;
    setTargetFormats(&ballDesc, true);
    ballDesc.debugName = "ball pipeline";
    const prisma::PipelineHandle ballPipeline = driver->createPipeline(ballDesc);

    prisma::PipelineDesc brightDesc;
    brightDesc.vertexShader = fullscreenVertex;
    brightDesc.fragmentShader = brightFragment;
    brightDesc.depthWrite = false;
    setTargetFormats(&brightDesc, false);
    brightDesc.debugName = "bright pipeline";
    const prisma::PipelineHandle brightPipeline = driver->createPipeline(brightDesc);

    prisma::PipelineDesc blurDesc = brightDesc;
    blurDesc.fragmentShader = blurFragment;
    blurDesc.debugName = "blur pipeline";
    const prisma::PipelineHandle blurPipeline = driver->createPipeline(blurDesc);

    prisma::PipelineDesc finalDesc;
    finalDesc.vertexShader = fullscreenVertex;
    finalDesc.fragmentShader = finalFragment;
    finalDesc.depthWrite = false;
    finalDesc.debugName = "final pipeline";
    const prisma::PipelineHandle finalPipeline = driver->createPipeline(finalDesc);

    driver->destroy(fullscreenVertex);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);
    driver->destroy(ballVertex);
    driver->destroy(ballFragment);
    driver->destroy(brightFragment);
    driver->destroy(blurFragment);
    driver->destroy(finalFragment);

    ready = ready && sampler.valid() && uniformBuffer.valid() && skyPipeline.valid() &&
            ballPipeline.valid() && brightPipeline.valid() && blurPipeline.valid() &&
            finalPipeline.valid();
    if (!ready) log_error("hdr: resource creation failed");

    Math::Vec3 center(0.0f, 0.0f, 0.0f);
    float size = 1.0f;
    if (ready) measure(mesh, &center, &size);
    const float scale = 1.0f / size;

    Targets targets;
    createTargets(driver, kRenderWidth, kRenderHeight, &targets);
    if (!targets.valid())
    {
        log_error("hdr: cannot create the render targets");
        ready = false;
    }
    float exposure = 1.0f;
    bool bloom = true;
    double previous = time_seconds();

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;

        const double now = time_seconds();
        const float dt = static_cast<float>(now - previous);
        previous = now;
        if (key_down(window, KEY_UP)) exposure *= expf(dt);
        if (key_down(window, KEY_DOWN)) exposure *= expf(-dt);
        if (exposure < 0.1f) exposure = 0.1f;
        if (exposure > 8.0f) exposure = 8.0f;
        if (key_pressed(window, KEY_B)) bloom = !bloom;

        const float aspect = static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight);
        const float time = (still ? 0.0f : static_cast<float>(now)) + 0.6f;
        const float angle = time * 0.25f;
        const Math::Vec3 eye(3.5f * sinf(angle), 0.6f, 3.5f * cosf(angle));

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view =
                Math::Mat4::LookAt(eye, Math::Vec3(0.0f, 0.0f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.inverseViewProjection = frame.viewProjection.Inverse();
        frame.model = Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                      Math::Mat4::Translation(-center);
        frame.cameraPosition[0] = eye.x;
        frame.cameraPosition[1] = eye.y;
        frame.cameraPosition[2] = eye.z;
        frame.cameraPosition[3] = 1.0f;
        memcpy(uniforms.data() + kRangeFrame * stride, &frame, sizeof(frame));

        PostUniforms post;
        post.params[0] = 1.0f;
        post.params[1] = post.params[2] = post.params[3] = 0.0f;
        memcpy(uniforms.data() + kRangeBright * stride, &post, sizeof(post));
        post.params[0] = 1.5f / static_cast<float>(targets.halfWidth);
        memcpy(uniforms.data() + kRangeBlurHorizontal * stride, &post, sizeof(post));
        post.params[0] = 0.0f;
        post.params[1] = 1.5f / static_cast<float>(targets.halfHeight);
        memcpy(uniforms.data() + kRangeBlurVertical * stride, &post, sizeof(post));
        post.params[0] = exposure;
        post.params[1] = bloom ? 0.8f : 0.0f;
        post.params[2] = post.params[3] = 0.0f;
        memcpy(uniforms.data() + kRangeFinal * stride, &post, sizeof(post));

        prisma::RenderPassDesc scenePass;
        scenePass.colors[0].texture = targets.sceneColor;
        scenePass.colorCount = 1;
        scenePass.depth.texture = targets.sceneDepth;
        scenePass.depthStore = prisma::StoreOp::Discard;
        scenePass.clearColor[0] = scenePass.clearColor[1] = scenePass.clearColor[2] = 0.0f;

        prisma::RenderPassDesc windowPass;
        windowPass.depthLoad = prisma::LoadOp::DontCare;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kRangeCount);

        driver->beginRenderPass(scenePass);
        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFrame * stride, sizeof(FrameUniforms));
        driver->bindTexture(0, environment, sampler);
        driver->draw(3, 0);
        driver->bindPipeline(ballPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFrame * stride, sizeof(FrameUniforms));
        driver->bindTexture(0, environment, sampler);
        for (unsigned i = 0; i < mesh.subsetCount(0); ++i) mesh.drawSubset(driver, 0, i);
        driver->endRenderPass();

        driver->beginRenderPass(postPass(targets.bloomA));
        driver->bindPipeline(brightPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeBright * stride, sizeof(PostUniforms));
        driver->bindTexture(0, targets.sceneColor, sampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(postPass(targets.bloomB));
        driver->bindPipeline(blurPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeBlurHorizontal * stride,
                sizeof(PostUniforms));
        driver->bindTexture(0, targets.bloomA, sampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(postPass(targets.bloomA));
        driver->bindPipeline(blurPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeBlurVertical * stride,
                sizeof(PostUniforms));
        driver->bindTexture(0, targets.bloomB, sampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(windowPass);
        driver->bindPipeline(finalPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFinal * stride, sizeof(PostUniforms));
        driver->bindTexture(0, targets.sceneColor, sampler);
        driver->bindTexture(1, targets.bloomA, sampler);
        driver->draw(3, 0);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    destroyTargets(driver, &targets);
    driver->destroy(finalPipeline);
    driver->destroy(blurPipeline);
    driver->destroy(brightPipeline);
    driver->destroy(ballPipeline);
    driver->destroy(skyPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(sampler);
    mesh.destroy(driver);
    driver->destroy(environment);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
