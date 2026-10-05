#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "contact_depth.frag.h"
#include "contact_depth.vert.h"
#include "contact_scene.frag.h"
#include "contact_scene.vert.h"

namespace
{

const std::uint32_t kShadowSize = 2048;

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 lightViewProjection;
    float lightDirection[4];
    float params[4];
    float extra[4];
};

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void accumulateBounds(const zenapp::SdkMesh& mesh, Math::Vec3* low, Math::Vec3* high)
{
    const zenapp::SdkMeshData& data = mesh.data();
    const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[data.meshes[0].vertexBuffers[0]];
    unsigned offset = 0;
    for (unsigned e = 0; e < 32 && buffer.decl[e].stream != 0xFF; ++e)
        if (buffer.decl[e].usage == 0) offset = buffer.decl[e].offset;

    for (uint64_t i = 0; i < buffer.numVertices; ++i)
    {
        float position[3];
        memcpy(position, data.file.data() + buffer.dataOffset + i * buffer.strideBytes + offset,
                sizeof(position));
        *low = Math::Vec3::Min(*low, Math::Vec3(position[0], position[1], position[2]));
        *high = Math::Vec3::Max(*high, Math::Vec3(position[0], position[1], position[2]));
    }
}

Math::Mat4 orthographicZeroToOne(float half, float depth)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Ortho(-half, half, -half, half, 0.0f, depth);
}

void drawMesh(prisma::Driver* driver, const zenapp::SdkMesh& mesh, prisma::TextureHandle white,
        prisma::SamplerHandle sampler, bool textured)
{
    for (unsigned i = 0; i < mesh.subsetCount(0); ++i)
    {
        if (textured)
        {
            prisma::TextureHandle diffuse = mesh.diffuse(mesh.subset(0, i).materialId);
            if (!diffuse.valid()) diffuse = white;
            driver->bindTexture(0, diffuse, sampler);
        }
        mesh.drawSubset(driver, 0, i);
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

    PlatformWindow* window = zenapp::openWindow("prisma 20 contact hardening", driverType);
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

    if (driver->caps().maxTextureSize < kShadowSize)
    {
        log_error("contact hardening: this GPU cannot create a %ux%u texture", kShadowSize,
                kShadowSize);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("ColumnScene/scene.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh scene;
    bool ready = scene.load(driver, path) && scene.meshCount() > 0;
    if (!ready) log_error("contact hardening: cannot load %s", path);

    zenapp::mediaPath("ColumnScene/Poles.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh poles;
    const bool havePoles = poles.load(driver, path) && poles.meshCount() > 0;
    if (!havePoles) log_error("contact hardening: cannot load %s, drawing the floor only", path);

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc textureDesc;
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.data = whitePixel;
    textureDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(textureDesc);

    prisma::TextureDesc shadowDesc;
    shadowDesc.format = prisma::TextureFormat::Depth32F;
    shadowDesc.width = kShadowSize;
    shadowDesc.height = kShadowSize;
    shadowDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    shadowDesc.debugName = "shadow map";
    const prisma::TextureHandle shadowMap = driver->createTexture(shadowDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "diffuse sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    prisma::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.minFilter = prisma::Filter::Linear;
    shadowSamplerDesc.magFilter = prisma::Filter::Linear;
    shadowSamplerDesc.mipFilter = prisma::MipFilter::None;
    shadowSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.compare = true;
    shadowSamplerDesc.compareOp = prisma::CompareOp::LessEqual;
    shadowSamplerDesc.debugName = "shadow sampler";
    const prisma::SamplerHandle shadowSampler = driver->createSampler(shadowSamplerDesc);

    prisma::SamplerDesc depthSamplerDesc;
    depthSamplerDesc.minFilter = prisma::Filter::Nearest;
    depthSamplerDesc.magFilter = prisma::Filter::Nearest;
    depthSamplerDesc.mipFilter = prisma::MipFilter::None;
    depthSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    depthSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    depthSamplerDesc.debugName = "shadow depth sampler";
    const prisma::SamplerHandle depthSampler = driver->createSampler(depthSamplerDesc);

    const std::uint32_t kRangeCount = 2;
    const std::uint32_t stride =
            alignUp(sizeof(FrameUniforms), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "shadow uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle depthVertex = zenapp::createShader(driver, contact_depth_vert);
    const prisma::ShaderHandle depthFragment = zenapp::createShader(driver, contact_depth_frag);
    const prisma::ShaderHandle sceneVertex = zenapp::createShader(driver, contact_scene_vert);
    const prisma::ShaderHandle sceneFragment = zenapp::createShader(driver, contact_scene_frag);

    prisma::PipelineDesc depthDesc;
    depthDesc.vertexShader = depthVertex;
    depthDesc.fragmentShader = depthFragment;
    if (ready) ready = scene.layout(0, &depthDesc);
    depthDesc.depthTest = true;
    depthDesc.cullMode = prisma::CullMode::Back;
    depthDesc.depthBiasConstant = 4.0f;
    depthDesc.depthBiasSlope = 2.0f;
    depthDesc.targets.window = false;
    depthDesc.targets.colorCount = 0;
    depthDesc.targets.depth = prisma::TextureFormat::Depth32F;
    depthDesc.debugName = "depth pipeline";
    const prisma::PipelineHandle depthPipeline = driver->createPipeline(depthDesc);

    prisma::PipelineDesc sceneDesc;
    sceneDesc.vertexShader = sceneVertex;
    sceneDesc.fragmentShader = sceneFragment;
    if (ready) ready = scene.layout(0, &sceneDesc);
    sceneDesc.depthTest = true;
    sceneDesc.cullMode = prisma::CullMode::Back;
    sceneDesc.debugName = "scene pipeline";
    const prisma::PipelineHandle scenePipeline = driver->createPipeline(sceneDesc);

    driver->destroy(depthVertex);
    driver->destroy(depthFragment);
    driver->destroy(sceneVertex);
    driver->destroy(sceneFragment);

    ready = ready && white.valid() && shadowMap.valid() && sampler.valid() &&
            shadowSampler.valid() && depthSampler.valid() && uniformBuffer.valid() &&
            depthPipeline.valid() && scenePipeline.valid();
    if (!ready) log_error("contact hardening: resource creation failed");

    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    if (ready)
    {
        accumulateBounds(scene, &low, &high);
        if (havePoles) accumulateBounds(poles, &low, &high);
    }
    const Math::Vec3 center = (low + high) * 0.5f;
    const float radius = ready ? (high - low).Length() * 0.5f : 1.0f;
    const Math::Mat4 lightProjection = orthographicZeroToOne(radius, radius * 2.0f);

    prisma::RenderPassDesc shadowPass;
    shadowPass.depth.texture = shadowMap;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.35f;
    pass.clearColor[1] = 0.45f;
    pass.clearColor[2] = 0.60f;

    float lightTan = 0.1f;
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
        const float dt = static_cast<float>(now - previous);
        previous = now;
        if (key_down(window, KEY_UP)) lightTan *= expf(dt);
        if (key_down(window, KEY_DOWN)) lightTan *= expf(-dt);
        if (lightTan < 0.005f) lightTan = 0.005f;
        if (lightTan > 0.5f) lightTan = 0.5f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const float cameraAngle = time * 0.1f;
        const Math::Vec3 eye(16.0f * sinf(cameraAngle), 9.0f, 16.0f * cosf(cameraAngle));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 200.0f);
        const Math::Mat4 view =
                Math::Mat4::LookAt(eye, Math::Vec3(0.0f, 1.5f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const float elevation = 0.5f;
        const Math::Vec3 light(cosf(time * 0.15f) * cosf(elevation), sinf(elevation),
                sinf(time * 0.15f) * cosf(elevation));
        const Math::Mat4 lightView =
                Math::Mat4::LookAt(center + light * radius, center, Math::Vec3(0.0f, 1.0f, 0.0f));
        const Math::Mat4 lightViewProjection = lightProjection * lightView;

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.lightViewProjection = lightViewProjection;
        frame.lightDirection[0] = light.x;
        frame.lightDirection[1] = light.y;
        frame.lightDirection[2] = light.z;
        frame.lightDirection[3] = 0.0f;
        const float depthRange = radius * 2.0f;
        const float texel = 1.0f / static_cast<float>(kShadowSize);
        frame.params[0] = 0.0003f;
        frame.params[1] = lightTan;
        frame.params[2] = depthRange / (radius * 2.0f);
        frame.params[3] = 0.02f;
        frame.extra[0] = radius * 2.0f * texel * 1.5f;
        frame.extra[1] = texel * 1.5f;
        frame.extra[2] = frame.extra[3] = 0.0f;
        memcpy(uniforms.data(), &lightViewProjection, sizeof(lightViewProjection));
        memcpy(uniforms.data() + stride, &frame, sizeof(frame));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kRangeCount);

        driver->beginRenderPass(shadowPass);
        driver->bindPipeline(depthPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(Math::Mat4));
        drawMesh(driver, scene, white, sampler, false);
        if (havePoles) drawMesh(driver, poles, white, sampler, false);
        driver->endRenderPass();

        driver->beginRenderPass(pass);
        driver->bindPipeline(scenePipeline);
        driver->bindUniformBuffer(0, uniformBuffer, stride, sizeof(FrameUniforms));
        driver->bindTexture(1, shadowMap, shadowSampler);
        driver->bindTexture(2, shadowMap, depthSampler);
        drawMesh(driver, scene, white, sampler, true);
        if (havePoles) drawMesh(driver, poles, white, sampler, true);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(scenePipeline);
    driver->destroy(depthPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(depthSampler);
    driver->destroy(shadowSampler);
    driver->destroy(sampler);
    driver->destroy(shadowMap);
    driver->destroy(white);
    poles.destroy(driver);
    scene.destroy(driver);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
