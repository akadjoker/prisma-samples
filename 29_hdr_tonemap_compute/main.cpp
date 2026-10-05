#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "adapt.comp.h"
#include "ball.frag.h"
#include "ball.vert.h"
#include "bloom_horizontal.comp.h"
#include "bloom_vertical.comp.h"
#include "final.frag.h"
#include "fullscreen.vert.h"
#include "luminance_reduce.comp.h"
#include "luminance_sum.comp.h"
#include "sky.frag.h"
#include "sky.vert.h"

namespace
{

const std::uint32_t kGrid = 81;
const std::uint32_t kReduceGroups = (kGrid + 7) / 8;
const std::uint32_t kPartials = kReduceGroups * kReduceGroups;
const std::uint32_t kBloomThreads = 128;
const std::uint32_t kBloomHalf = 7;
const std::uint32_t kBloomStep = kBloomThreads - kBloomHalf * 2;
const float kBloomStrength = 0.6f;
const float kPi = 3.14159265f;

const float kLevelScale[3] = { 0.04f, 1.0f, 16.0f };
const char* const kLevelName[3] = { "dark", "normal", "very bright" };

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 inverseViewProjection;
    Math::Mat4 model;
    float cameraPosition[4];
    float params[4];
};

struct BloomUniforms
{
    float weights[15][4];
    std::int32_t size[4];
};

enum Range : std::uint32_t
{
    kRangeFrame,
    kRangeReduce,
    kRangeSum,
    kRangeAdapt,
    kRangeBloomHorizontal,
    kRangeBloomVertical,
    kRangeFinal,
    kRangeCount
};

const std::uint32_t kRenderWidth = 1280;
const std::uint32_t kRenderHeight = 720;

struct Targets
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bloomWidth = 0;
    std::uint32_t bloomHeight = 0;
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

std::uint32_t divideUp(std::uint32_t value, std::uint32_t divisor)
{
    return (value + divisor - 1) / divisor;
}

prisma::TextureHandle createTarget(prisma::Driver* driver, std::uint32_t width,
        std::uint32_t height, prisma::TextureFormat format, std::uint32_t usage, const char* name)
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
    targets->width = width;
    targets->height = height;
    targets->bloomWidth = width / 8 > 0 ? width / 8 : 1;
    targets->bloomHeight = height / 8 > 0 ? height / 8 : 1;
    targets->sceneColor = createTarget(driver, width, height, prisma::TextureFormat::RGBA16F,
            prisma::kTextureSampled | prisma::kTextureRenderTarget, "scene color");
    targets->sceneDepth = createTarget(driver, width, height, prisma::TextureFormat::Depth32F,
            prisma::kTextureRenderTarget, "scene depth");
    targets->bloomA = createTarget(driver, targets->bloomWidth, targets->bloomHeight,
            prisma::TextureFormat::RGBA16F, prisma::kTextureSampled | prisma::kTextureStorage,
            "bloom a");
    targets->bloomB = createTarget(driver, targets->bloomWidth, targets->bloomHeight,
            prisma::TextureFormat::RGBA16F, prisma::kTextureSampled | prisma::kTextureStorage,
            "bloom b");
}

void destroyTargets(prisma::Driver* driver, Targets* targets)
{
    if (targets->bloomB.valid()) driver->destroy(targets->bloomB);
    if (targets->bloomA.valid()) driver->destroy(targets->bloomA);
    if (targets->sceneDepth.valid()) driver->destroy(targets->sceneDepth);
    if (targets->sceneColor.valid()) driver->destroy(targets->sceneColor);
    *targets = Targets();
}

float gaussian(float x, float deviation)
{
    return expf(-(x * x) / (2.0f * deviation * deviation)) /
           sqrtf(2.0f * kPi * deviation * deviation);
}

void fillWeights(BloomUniforms* uniforms, float deviation, float multiplier)
{
    for (int i = 0; i < 15; ++i)
    {
        const int distance = i < 7 ? 7 - i : i - 7;
        const float weight =
                (distance == 0 ? 1.0f : multiplier) * gaussian(static_cast<float>(distance),
                                                               deviation);
        uniforms->weights[i][0] = uniforms->weights[i][1] = uniforms->weights[i][2] = weight;
        uniforms->weights[i][3] = 1.0f;
    }
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

void setSceneFormats(prisma::PipelineDesc* desc)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = prisma::TextureFormat::RGBA16F;
    desc->targets.depth = prisma::TextureFormat::Depth32F;
}

prisma::PipelineHandle createComputeStage(prisma::Driver* driver, const prisma::ShaderBlob& blob,
        const prisma::ComputePipelineDesc& layout, const char* name)
{
    const prisma::ShaderHandle shader = zenapp::createShader(driver, blob);
    prisma::ComputePipelineDesc desc = layout;
    desc.shader = shader;
    desc.debugName = name;
    const prisma::PipelineHandle pipeline = driver->createComputePipeline(desc);
    driver->destroy(shader);
    return pipeline;
}

bool finiteValue(float value) { return value == value && fabsf(value) < 1e30f; }

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");

    int level = 1;
    if (zenapp::hasArgument(argc, argv, "dark")) level = 0;
    if (zenapp::hasArgument(argc, argv, "bright")) level = 2;

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 29 hdr tonemap compute", driverType);
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

    if (!driver->caps().compute || !driver->caps().storageBuffersInGraphics ||
            !driver->caps().floatColorTargets)
    {
        log_error("hdr tonemap compute: this GPU has no compute shaders, no storage buffers in "
                  "fragment shaders or no float colour targets");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("Light Probes/grace_cross.dds", path, sizeof(path));
    const prisma::TextureHandle environment = zenapp::loadTexture(driver, path, false, false);
    if (!environment.valid())
    {
        log_error("hdr tonemap compute: cannot create %s on this GPU", path);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    zenapp::mediaPath("misc/ball.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh mesh;
    bool ready = mesh.load(driver, path, false) && mesh.meshCount() > 0;
    if (!ready) log_error("hdr tonemap compute: cannot load %s", path);

    prisma::SamplerDesc nearestDesc;
    nearestDesc.minFilter = prisma::Filter::Nearest;
    nearestDesc.magFilter = prisma::Filter::Nearest;
    nearestDesc.mipFilter = prisma::MipFilter::None;
    nearestDesc.addressU = prisma::AddressMode::ClampToEdge;
    nearestDesc.addressV = prisma::AddressMode::ClampToEdge;
    nearestDesc.addressW = prisma::AddressMode::ClampToEdge;
    nearestDesc.debugName = "nearest sampler";
    const prisma::SamplerHandle nearest = driver->createSampler(nearestDesc);

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc linearDesc = nearestDesc;
    linearDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    linearDesc.magFilter = linearDesc.minFilter;
    linearDesc.mipFilter = linear ? prisma::MipFilter::Linear : prisma::MipFilter::None;
    linearDesc.debugName = "linear sampler";
    const prisma::SamplerHandle bilinear = driver->createSampler(linearDesc);

    const std::uint32_t stride =
            alignUp(sizeof(FrameUniforms) > sizeof(BloomUniforms) ? sizeof(FrameUniforms)
                                                                  : sizeof(BloomUniforms),
                    driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "hdr uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const float initialLuminance[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = sizeof(initialLuminance);
    bufferDesc.data = initialLuminance;
    bufferDesc.debugName = "adapted luminance";
    const prisma::BufferHandle luminanceBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = kPartials * sizeof(float);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "luminance partial sums";
    const prisma::BufferHandle partialBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = 4 * sizeof(float);
    bufferDesc.debugName = "luminance sum";
    const prisma::BufferHandle sumBuffer = driver->createBuffer(bufferDesc);

    prisma::ComputePipelineDesc layout;
    layout.uniformBlockCount = 1;
    layout.uniformBlocks[0].name = "Params";
    layout.uniformBlocks[0].slot = 0;

    prisma::ComputePipelineDesc reduceLayout = layout;
    reduceLayout.textureCount = 1;
    reduceLayout.textures[0].name = "uInput";
    reduceLayout.textures[0].slot = 0;
    reduceLayout.storageBufferCount = 1;
    reduceLayout.storageBuffers[0].name = "Result";
    reduceLayout.storageBuffers[0].slot = 0;
    const prisma::PipelineHandle reducePipeline = createComputeStage(driver,
            luminance_reduce_comp, reduceLayout, "luminance reduce");

    prisma::ComputePipelineDesc sumLayout = layout;
    sumLayout.storageBufferCount = 2;
    sumLayout.storageBuffers[0].name = "Input";
    sumLayout.storageBuffers[0].slot = 0;
    sumLayout.storageBuffers[1].name = "Result";
    sumLayout.storageBuffers[1].slot = 1;
    const prisma::PipelineHandle sumPipeline =
            createComputeStage(driver, luminance_sum_comp, sumLayout, "luminance sum");

    prisma::ComputePipelineDesc adaptLayout = layout;
    adaptLayout.storageBufferCount = 2;
    adaptLayout.storageBuffers[0].name = "Sum";
    adaptLayout.storageBuffers[0].slot = 0;
    adaptLayout.storageBuffers[1].name = "Luminance";
    adaptLayout.storageBuffers[1].slot = 1;
    const prisma::PipelineHandle adaptPipeline =
            createComputeStage(driver, adapt_comp, adaptLayout, "adapt");

    prisma::ComputePipelineDesc horizontalLayout = layout;
    horizontalLayout.textureCount = 1;
    horizontalLayout.textures[0].name = "uInput";
    horizontalLayout.textures[0].slot = 0;
    horizontalLayout.storageBufferCount = 1;
    horizontalLayout.storageBuffers[0].name = "Luminance";
    horizontalLayout.storageBuffers[0].slot = 0;
    horizontalLayout.storageTextureCount = 1;
    horizontalLayout.storageTextures[0].name = "uOutput";
    horizontalLayout.storageTextures[0].slot = 0;
    const prisma::PipelineHandle horizontalPipeline = createComputeStage(driver,
            bloom_horizontal_comp, horizontalLayout, "bloom bright pass and horizontal blur");

    prisma::ComputePipelineDesc verticalLayout = layout;
    verticalLayout.textureCount = 1;
    verticalLayout.textures[0].name = "uInput";
    verticalLayout.textures[0].slot = 0;
    verticalLayout.storageTextureCount = 1;
    verticalLayout.storageTextures[0].name = "uOutput";
    verticalLayout.storageTextures[0].slot = 0;
    const prisma::PipelineHandle verticalPipeline = createComputeStage(driver,
            bloom_vertical_comp, verticalLayout, "bloom vertical blur");

    const prisma::ShaderHandle fullscreenVertex = zenapp::createShader(driver, fullscreen_vert);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);
    const prisma::ShaderHandle ballVertex = zenapp::createShader(driver, ball_vert);
    const prisma::ShaderHandle ballFragment = zenapp::createShader(driver, ball_frag);
    const prisma::ShaderHandle finalFragment = zenapp::createShader(driver, final_frag);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    setSceneFormats(&skyDesc);
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    prisma::PipelineDesc ballDesc;
    ballDesc.vertexShader = ballVertex;
    ballDesc.fragmentShader = ballFragment;
    if (ready) ready = mesh.layout(0, &ballDesc);
    ballDesc.depthTest = true;
    ballDesc.cullMode = prisma::CullMode::Back;
    setSceneFormats(&ballDesc);
    ballDesc.debugName = "ball pipeline";
    const prisma::PipelineHandle ballPipeline = driver->createPipeline(ballDesc);

    prisma::PipelineDesc finalDesc;
    finalDesc.vertexShader = fullscreenVertex;
    finalDesc.fragmentShader = finalFragment;
    finalDesc.depthWrite = false;
    finalDesc.storageBufferCount = 1;
    finalDesc.storageBuffers[0].name = "Luminance";
    finalDesc.storageBuffers[0].slot = 0;
    finalDesc.debugName = "final pipeline";
    const prisma::PipelineHandle finalPipeline = driver->createPipeline(finalDesc);

    driver->destroy(fullscreenVertex);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);
    driver->destroy(ballVertex);
    driver->destroy(ballFragment);
    driver->destroy(finalFragment);

    ready = ready && nearest.valid() && bilinear.valid() && uniformBuffer.valid() &&
            luminanceBuffer.valid() && partialBuffer.valid() && sumBuffer.valid() &&
            reducePipeline.valid() && sumPipeline.valid() && adaptPipeline.valid() &&
            horizontalPipeline.valid() && verticalPipeline.valid() && skyPipeline.valid() &&
            ballPipeline.valid() && finalPipeline.valid();
    if (!ready) log_error("hdr tonemap compute: resource creation failed");

    Math::Vec3 center(0.0f, 0.0f, 0.0f);
    float size = 1.0f;
    if (ready) measure(mesh, &center, &size);
    const float scale = 1.0f / size;

    Targets targets;
    createTargets(driver, kRenderWidth, kRenderHeight, &targets);
    if (!targets.valid())
    {
        log_error("hdr tonemap compute: cannot create the render targets");
        ready = false;
    }
    bool bloom = true;
    double previous = time_seconds();
    log_info("hdr tonemap compute: scene brightness %s", kLevelName[level]);

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
        float dt = static_cast<float>(now - previous);
        previous = now;
        if (dt > 0.1f) dt = 0.1f;
        if (still) dt = frames == 0 ? 20.0f : 1.0f / 30.0f;
        if (key_pressed(window, KEY_B)) bloom = !bloom;
        if (key_pressed(window, KEY_L))
        {
            level = (level + 1) % 3;
            log_info("hdr tonemap compute: scene brightness %s", kLevelName[level]);
        }

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
        frame.params[0] = kLevelScale[level];
        frame.params[1] = frame.params[2] = frame.params[3] = 0.0f;
        memcpy(uniforms.data() + kRangeFrame * stride, &frame, sizeof(frame));

        std::uint32_t reduceParams[4] = { kReduceGroups, kReduceGroups, targets.width,
            targets.height };
        memcpy(uniforms.data() + kRangeReduce * stride, reduceParams, sizeof(reduceParams));
        std::uint32_t sumParams[4] = { kPartials, 0, 0, 0 };
        memcpy(uniforms.data() + kRangeSum * stride, sumParams, sizeof(sumParams));
        float adaptParams[4] = { dt, 1.0f / static_cast<float>(kGrid * kGrid), 0.0f, 0.0f };
        memcpy(uniforms.data() + kRangeAdapt * stride, adaptParams, sizeof(adaptParams));

        BloomUniforms bloomUniforms;
        fillWeights(&bloomUniforms, 3.0f, 1.25f);
        bloomUniforms.size[0] = static_cast<std::int32_t>(targets.bloomWidth);
        bloomUniforms.size[1] = static_cast<std::int32_t>(targets.bloomHeight);
        bloomUniforms.size[2] = static_cast<std::int32_t>(targets.width);
        bloomUniforms.size[3] = static_cast<std::int32_t>(targets.height);
        memcpy(uniforms.data() + kRangeBloomHorizontal * stride, &bloomUniforms,
                sizeof(bloomUniforms));
        bloomUniforms.size[2] = bloomUniforms.size[0];
        bloomUniforms.size[3] = bloomUniforms.size[1];
        memcpy(uniforms.data() + kRangeBloomVertical * stride, &bloomUniforms,
                sizeof(bloomUniforms));

        float finalParams[4] = { bloom ? kBloomStrength : 0.0f, 0.0f, 0.0f, 0.0f };
        memcpy(uniforms.data() + kRangeFinal * stride, finalParams, sizeof(finalParams));

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
        driver->bindTexture(0, environment, bilinear);
        driver->draw(3, 0);
        driver->bindPipeline(ballPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFrame * stride, sizeof(FrameUniforms));
        driver->bindTexture(0, environment, bilinear);
        for (unsigned i = 0; i < mesh.subsetCount(0); ++i) mesh.drawSubset(driver, 0, i);
        driver->endRenderPass();

        driver->beginComputePass();

        driver->bindPipeline(reducePipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeReduce * stride, 16);
        driver->bindTexture(0, targets.sceneColor, nearest);
        driver->bindStorageBuffer(0, partialBuffer, 0, kPartials * sizeof(float));
        driver->dispatch(kReduceGroups, kReduceGroups, 1);

        driver->bindPipeline(sumPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeSum * stride, 16);
        driver->bindStorageBuffer(0, partialBuffer, 0, kPartials * sizeof(float));
        driver->bindStorageBuffer(1, sumBuffer, 0, 4 * sizeof(float));
        driver->dispatch(divideUp(kPartials, 128), 1, 1);

        driver->bindPipeline(adaptPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeAdapt * stride, 16);
        driver->bindStorageBuffer(0, sumBuffer, 0, 4 * sizeof(float));
        driver->bindStorageBuffer(1, luminanceBuffer, 0, sizeof(initialLuminance));
        driver->dispatch(1, 1, 1);

        if (bloom)
        {
            driver->bindPipeline(horizontalPipeline);
            driver->bindUniformBuffer(0, uniformBuffer, kRangeBloomHorizontal * stride,
                    sizeof(BloomUniforms));
            driver->bindTexture(0, targets.sceneColor, nearest);
            driver->bindStorageBuffer(0, luminanceBuffer, 0, sizeof(initialLuminance));
            driver->bindStorageTexture(0, targets.bloomA, 0, prisma::StorageAccess::Write);
            driver->dispatch(divideUp(targets.bloomWidth, kBloomStep), targets.bloomHeight, 1);

            driver->bindPipeline(verticalPipeline);
            driver->bindUniformBuffer(0, uniformBuffer, kRangeBloomVertical * stride,
                    sizeof(BloomUniforms));
            driver->bindTexture(0, targets.bloomA, nearest);
            driver->bindStorageTexture(0, targets.bloomB, 0, prisma::StorageAccess::Write);
            driver->dispatch(targets.bloomWidth, divideUp(targets.bloomHeight, kBloomStep), 1);
        }
        driver->endComputePass();

        driver->beginRenderPass(windowPass);
        driver->bindPipeline(finalPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFinal * stride, 16);
        driver->bindTexture(0, targets.sceneColor, nearest);
        driver->bindTexture(1, bloom ? targets.bloomB : targets.sceneColor, bilinear);
        driver->bindStorageBuffer(0, luminanceBuffer, 0, sizeof(initialLuminance));
        driver->draw(3, 0);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        ++frames;
        if (maxFrames > 0 && frames >= maxFrames) window_set_should_close(window, true);
    }

    bool passed = ready;
    if (ready)
    {
        float result[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (!driver->readBuffer(luminanceBuffer, 0, sizeof(result), result))
        {
            log_error("hdr tonemap compute: FAIL, could not read the luminance back");
            passed = false;
        }
        else
        {
            const float adapted = result[0];
            const float measured = result[1];
            log_info("hdr tonemap compute: scene %s, measured average luminance %.5f, adapted "
                     "luminance %.5f",
                    kLevelName[level], measured, adapted);
            bool sane = finiteValue(adapted) && finiteValue(measured) && adapted > 1e-6f &&
                        adapted < 1e6f && measured > 1e-6f && measured < 1e6f;
            if (sane && still && fabsf(adapted - measured) > 0.01f * measured) sane = false;
            if (sane)
                log_info("hdr tonemap compute: PASS, finite and %s",
                        still ? "converged to the measured luminance" : "within range");
            else
                log_error("hdr tonemap compute: FAIL, the adapted luminance is not finite, out "
                          "of range or not converged");
            passed = sane;
        }
    }

    destroyTargets(driver, &targets);
    if (finalPipeline.valid()) driver->destroy(finalPipeline);
    if (ballPipeline.valid()) driver->destroy(ballPipeline);
    if (skyPipeline.valid()) driver->destroy(skyPipeline);
    if (verticalPipeline.valid()) driver->destroy(verticalPipeline);
    if (horizontalPipeline.valid()) driver->destroy(horizontalPipeline);
    if (adaptPipeline.valid()) driver->destroy(adaptPipeline);
    if (sumPipeline.valid()) driver->destroy(sumPipeline);
    if (reducePipeline.valid()) driver->destroy(reducePipeline);
    if (sumBuffer.valid()) driver->destroy(sumBuffer);
    if (partialBuffer.valid()) driver->destroy(partialBuffer);
    if (luminanceBuffer.valid()) driver->destroy(luminanceBuffer);
    if (uniformBuffer.valid()) driver->destroy(uniformBuffer);
    if (bilinear.valid()) driver->destroy(bilinear);
    if (nearest.valid()) driver->destroy(nearest);
    mesh.destroy(driver);
    driver->destroy(environment);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return passed ? 0 : 1;
}
