#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "variance_blur.frag.h"
#include "variance_depth.frag.h"
#include "variance_depth.vert.h"
#include "variance_fullscreen.vert.h"
#include "variance_scene.frag.h"
#include "variance_scene.vert.h"

namespace
{

const std::uint32_t kMomentSize = 2048;
const int kMaxBlurRadius = 16;

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 view;
    Math::Mat4 lightViewProjection;
    float lightDirection[4];
    float params[4];
    float fog[4];
};

struct BlurUniforms
{
    float params[4];
};

enum Range : std::uint32_t
{
    kRangeLight,
    kRangeFrame,
    kRangeBlurHorizontal,
    kRangeBlurVertical,
    kRangeCount
};

struct Scene
{
    zenapp::SdkMesh mesh;
    bool loaded = false;
    bool columns = false;
    ct::Vector<Math::Box> bounds;
    ct::Vector<unsigned> layoutOf;
    ct::Vector<prisma::PipelineHandle> momentPipelines;
    ct::Vector<prisma::PipelineHandle> scenePipelines;
    Math::Box total;
    Math::Box shadowBox;
    Math::Vec3 toLight;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

Math::Mat4 orthographicZeroToOne(float left, float right, float bottom, float top, float nearPlane,
        float farPlane)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Ortho(left, right, bottom, top, nearPlane, farPlane);
}

void meshBounds(const zenapp::SdkMeshData& data, unsigned mesh, Math::Box* box)
{
    const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[data.meshes[mesh].vertexBuffers[0]];
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
    *box = Math::Box(low, high);
}

bool sameLayout(const zenapp::SdkMesh& mesh, unsigned a, unsigned b)
{
    prisma::PipelineDesc first;
    prisma::PipelineDesc other;
    if (!mesh.layout(a, &first) || !mesh.layout(b, &other)) return false;
    if (first.attributeCount != other.attributeCount ||
            first.vertexBufferCount != other.vertexBufferCount)
        return false;
    for (std::uint32_t i = 0; i < first.attributeCount; ++i)
        if (first.attributes[i].location != other.attributes[i].location ||
                first.attributes[i].format != other.attributes[i].format ||
                first.attributes[i].offset != other.attributes[i].offset ||
                first.attributes[i].buffer != other.attributes[i].buffer)
            return false;
    for (std::uint32_t i = 0; i < first.vertexBufferCount; ++i)
        if (first.vertexBuffers[i].stride != other.vertexBuffers[i].stride) return false;
    return true;
}

bool findLight(const zenapp::SdkMeshData& data, Math::Vec3* toLight)
{
    for (size_t i = 0; i < data.frames.size(); ++i)
    {
        if (strncmp(data.frames[i].name, "directionalLight", 16) != 0) continue;
        Math::Mat4 world = Math::Mat4::Identity();
        uint32_t frame = static_cast<uint32_t>(i);
        for (int depth = 0; frame < data.frames.size() && depth < 64; ++depth)
        {
            const float* m = data.frames[frame].matrix;
            const Math::Mat4 local(Math::Vec4(m[0], m[1], m[2], m[3]),
                    Math::Vec4(m[4], m[5], m[6], m[7]), Math::Vec4(m[8], m[9], m[10], m[11]),
                    Math::Vec4(m[12], m[13], m[14], m[15]));
            world = local * world;
            frame = data.frames[frame].parentFrame;
        }
        const Math::Vec3 direction =
                Math::Vec3(world.col2.x, world.col2.y, world.col2.z).Normalized();
        *toLight = direction * -1.0f;
        return toLight->y > 0.1f;
    }
    return false;
}

bool buildScene(prisma::Driver* driver, const char* relative, bool columns,
        const prisma::ShaderHandle* shaders, Scene* scene)
{
    char path[1024];
    zenapp::mediaPath(relative, path, sizeof(path));
    if (!scene->mesh.load(driver, path, true) || scene->mesh.meshCount() == 0)
    {
        log_error("variance shadows: cannot load %s", path);
        return false;
    }
    scene->columns = columns;

    const zenapp::SdkMesh& mesh = scene->mesh;
    ct::Vector<unsigned> representatives;
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (unsigned m = 0; m < mesh.meshCount(); ++m)
    {
        unsigned layout = 0;
        while (layout < representatives.size() && !sameLayout(mesh, representatives[layout], m))
            ++layout;
        if (layout == representatives.size()) representatives.push_back(m);
        scene->layoutOf.push_back(layout);

        Math::Box box;
        meshBounds(mesh.data(), m, &box);
        scene->bounds.push_back(box);
        low = Math::Vec3::Min(low, box.min);
        high = Math::Vec3::Max(high, box.max);
    }
    scene->total = Math::Box(low, high);

    for (size_t l = 0; l < representatives.size(); ++l)
    {
        prisma::PipelineDesc momentDesc;
        momentDesc.vertexShader = shaders[0];
        momentDesc.fragmentShader = shaders[1];
        if (!mesh.layout(representatives[l], &momentDesc)) return false;
        momentDesc.depthTest = true;
        momentDesc.cullMode = prisma::CullMode::Back;
        momentDesc.targets.window = false;
        momentDesc.targets.colorCount = 1;
        momentDesc.targets.colors[0] = prisma::TextureFormat::RG32F;
        momentDesc.targets.depth = prisma::TextureFormat::Depth32F;
        momentDesc.debugName = "moment pipeline";
        scene->momentPipelines.push_back(driver->createPipeline(momentDesc));

        prisma::PipelineDesc sceneDesc;
        sceneDesc.vertexShader = shaders[2];
        sceneDesc.fragmentShader = shaders[3];
        if (!mesh.layout(representatives[l], &sceneDesc)) return false;
        sceneDesc.depthTest = true;
        sceneDesc.cullMode = prisma::CullMode::Back;
        sceneDesc.debugName = "variance scene pipeline";
        scene->scenePipelines.push_back(driver->createPipeline(sceneDesc));

        if (!scene->momentPipelines[l].valid() || !scene->scenePipelines[l].valid()) return false;
    }

    const Math::Vec3 size = high - low;
    if (columns)
    {
        scene->nearPlane = 0.1f;
        scene->farPlane = 80.0f;
        scene->toLight = Math::Vec3(0.6f, 0.55f, -0.4f).Normalized();
        scene->shadowBox =
                Math::Box(Math::Vec3(-180.0f, -2.0f, -60.0f), Math::Vec3(100.0f, 20.0f, 60.0f));
    }
    else
    {
        const float radius = size.Length() * 0.5f;
        scene->nearPlane = radius * 0.001f > 0.1f ? radius * 0.001f : 0.1f;
        scene->farPlane = radius * 0.9f;
        if (!findLight(mesh.data(), &scene->toLight))
            scene->toLight = Math::Vec3(0.5f, 0.7f, 0.3f).Normalized();
        scene->shadowBox = scene->total;
    }
    scene->loaded = true;
    return true;
}

void destroyScene(prisma::Driver* driver, Scene* scene)
{
    for (size_t i = 0; i < scene->scenePipelines.size(); ++i)
        driver->destroy(scene->scenePipelines[i]);
    for (size_t i = 0; i < scene->momentPipelines.size(); ++i)
        driver->destroy(scene->momentPipelines[i]);
    scene->mesh.destroy(driver);
}

Math::Mat4 fitLight(const Scene& scene, float* texelWorld)
{
    const Math::Vec3 up(0.0f, 1.0f, 0.0f);
    const Math::Vec3 center = scene.shadowBox.Center();
    const Math::Mat4 view = Math::Mat4::LookAt(center + scene.toLight, center, up);
    Math::Vec3 corners[8];
    scene.shadowBox.GetCorners(corners);
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        const Math::Vec3 p = view.TransformPoint(corners[i]);
        low = Math::Vec3::Min(low, p);
        high = Math::Vec3::Max(high, p);
    }
    const float width = high.x - low.x;
    const float height = high.y - low.y;
    *texelWorld = (width > height ? width : height) / static_cast<float>(kMomentSize);
    return orthographicZeroToOne(low.x, high.x, low.y, high.y, -high.z - 1.0f, -low.z + 1.0f) *
           view;
}

void drawVisible(prisma::Driver* driver, const Scene& scene, bool momentsOnly,
        prisma::TextureHandle white, prisma::SamplerHandle sampler,
        const Math::Mat4& viewProjection)
{
    const Math::Frustum frustum = Math::Frustum::FromViewProjection(viewProjection);
    unsigned current = 0xFFFFFFFFu;
    for (unsigned m = 0; m < scene.mesh.meshCount(); ++m)
    {
        if (!frustum.IntersectsBox(scene.bounds[m])) continue;
        if (scene.layoutOf[m] != current)
        {
            current = scene.layoutOf[m];
            driver->bindPipeline(
                    momentsOnly ? scene.momentPipelines[current] : scene.scenePipelines[current]);
        }
        for (unsigned i = 0; i < scene.mesh.subsetCount(m); ++i)
        {
            if (!momentsOnly)
            {
                prisma::TextureHandle diffuse =
                        scene.mesh.diffuse(scene.mesh.subset(m, i).materialId);
                driver->bindTexture(0, diffuse.valid() ? diffuse : white, sampler);
            }
            scene.mesh.drawSubset(driver, m, i);
        }
    }
}

void cameraPath(const Scene& scene, float time, Math::Vec3* eye, Math::Vec3* target)
{
    const float angle = time * 0.12f;
    const float lead = angle + 0.9f;
    if (scene.columns)
    {
        *eye = Math::Vec3(-40.0f + 60.0f * sinf(angle), 9.0f + 1.5f * sinf(angle * 1.7f),
                16.0f + 6.0f * cosf(angle * 2.0f));
        *target = Math::Vec3(-40.0f + 60.0f * sinf(lead), 3.0f, 0.0f);
        return;
    }
    const Math::Vec3 center = scene.total.Center();
    const Math::Vec3 half = scene.total.Extents();
    const Math::Vec3 low = scene.total.min;
    *eye = Math::Vec3(center.x + half.x * 0.8f * sinf(angle),
            low.y + half.y * (2.3f + 0.05f * sinf(angle * 1.7f)),
            center.z + half.z * 0.8f * cosf(angle));
    *target = Math::Vec3(center.x + half.x * 0.2f * sinf(lead), low.y + half.y * 0.5f,
            center.z + half.z * 0.2f * cosf(lead));
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

    PlatformWindow* window = zenapp::openWindow("prisma 19 variance shadows", driverType);
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

    if (!driver->caps().floatColorTargets || driver->caps().maxTextureSize < kMomentSize)
    {
        log_error("variance shadows: this GPU cannot render %ux%u float colour targets",
                kMomentSize, kMomentSize);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.data = whitePixel;
    whiteDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(whiteDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "diffuse sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const std::uint32_t both = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    prisma::TextureDesc momentDesc;
    momentDesc.format = prisma::TextureFormat::RG32F;
    momentDesc.width = kMomentSize;
    momentDesc.height = kMomentSize;
    momentDesc.usage = both;
    momentDesc.debugName = "moments a";
    const prisma::TextureHandle momentsA = driver->createTexture(momentDesc);
    momentDesc.debugName = "moments b";
    const prisma::TextureHandle momentsB = driver->createTexture(momentDesc);

    prisma::TextureDesc depthDesc;
    depthDesc.format = prisma::TextureFormat::Depth32F;
    depthDesc.width = kMomentSize;
    depthDesc.height = kMomentSize;
    depthDesc.usage = prisma::kTextureRenderTarget;
    depthDesc.debugName = "moment depth";
    const prisma::TextureHandle momentDepth = driver->createTexture(depthDesc);

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc momentSamplerDesc;
    momentSamplerDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    momentSamplerDesc.magFilter = momentSamplerDesc.minFilter;
    momentSamplerDesc.mipFilter = prisma::MipFilter::None;
    momentSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    momentSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    momentSamplerDesc.debugName = "moment sampler";
    const prisma::SamplerHandle momentSampler = driver->createSampler(momentSamplerDesc);

    const std::uint32_t stride =
            alignUp(sizeof(FrameUniforms), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "variance uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    prisma::ShaderHandle shaders[4];
    shaders[0] = zenapp::createShader(driver, variance_depth_vert);
    shaders[1] = zenapp::createShader(driver, variance_depth_frag);
    shaders[2] = zenapp::createShader(driver, variance_scene_vert);
    shaders[3] = zenapp::createShader(driver, variance_scene_frag);
    const prisma::ShaderHandle fullscreenVertex =
            zenapp::createShader(driver, variance_fullscreen_vert);
    const prisma::ShaderHandle blurFragment = zenapp::createShader(driver, variance_blur_frag);

    prisma::PipelineDesc blurDesc;
    blurDesc.vertexShader = fullscreenVertex;
    blurDesc.fragmentShader = blurFragment;
    blurDesc.depthWrite = false;
    blurDesc.targets.window = false;
    blurDesc.targets.colorCount = 1;
    blurDesc.targets.colors[0] = prisma::TextureFormat::RG32F;
    blurDesc.debugName = "blur pipeline";
    const prisma::PipelineHandle blurPipeline = driver->createPipeline(blurDesc);

    Scene scenes[2];
    bool ready = white.valid() && sampler.valid() && momentsA.valid() && momentsB.valid() &&
                 momentDepth.valid() && momentSampler.valid() && uniformBuffer.valid() &&
                 blurPipeline.valid();
    if (ready)
        ready = buildScene(driver, "powerplant/powerplant.sdkmesh", false, shaders, &scenes[0]);
    if (ready) buildScene(driver, "ShadowColumns/testscene.sdkmesh", true, shaders, &scenes[1]);
    if (!ready) log_error("variance shadows: resource creation failed");

    for (int i = 0; i < 4; ++i) driver->destroy(shaders[i]);
    driver->destroy(fullscreenVertex);
    driver->destroy(blurFragment);

    prisma::RenderPassDesc momentPass;
    momentPass.colors[0].texture = momentsA;
    momentPass.colorCount = 1;
    momentPass.depth.texture = momentDepth;
    momentPass.depthStore = prisma::StoreOp::Discard;
    momentPass.clearColor[0] = 1.0f;
    momentPass.clearColor[1] = 1.0f;
    momentPass.clearColor[2] = 0.0f;
    momentPass.clearColor[3] = 0.0f;

    prisma::RenderPassDesc blurHorizontal;
    blurHorizontal.colors[0].texture = momentsB;
    blurHorizontal.colorCount = 1;
    blurHorizontal.depthLoad = prisma::LoadOp::DontCare;
    blurHorizontal.stencilLoad = prisma::LoadOp::DontCare;
    prisma::RenderPassDesc blurVertical = blurHorizontal;
    blurVertical.colors[0].texture = momentsA;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.55f;
    pass.clearColor[1] = 0.68f;
    pass.clearColor[2] = 0.85f;

    int active = zenapp::hasArgument(argc, argv, "columns") && scenes[1].loaded ? 1 : 0;
    int radius = 3;
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_UP) && radius < kMaxBlurRadius) ++radius;
        if (key_pressed(window, KEY_DOWN) && radius > 0) --radius;
        if (key_pressed(window, KEY_T) && scenes[1].loaded) active = 1 - active;
        const Scene& scene = scenes[active];

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 24.0f;

        Math::Vec3 eye;
        Math::Vec3 target;
        cameraPath(scene, time, &eye, &target);
        const Math::Mat4 projection =
                zenapp::perspectiveZeroToOne(1.0f, aspect, scene.nearPlane, scene.farPlane);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, target, Math::Vec3(0.0f, 1.0f, 0.0f));

        float texelWorld = 1.0f;
        const Math::Mat4 lightViewProjection = fitLight(scene, &texelWorld);

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.view = view;
        frame.lightViewProjection = lightViewProjection;
        frame.lightDirection[0] = scene.toLight.x;
        frame.lightDirection[1] = scene.toLight.y;
        frame.lightDirection[2] = scene.toLight.z;
        frame.lightDirection[3] = 0.0f;
        frame.params[0] = texelWorld * 1.5f;
        frame.params[1] = 0.25f;
        frame.params[2] = 0.00002f;
        frame.params[3] = 0.0f;
        frame.fog[0] = scene.farPlane * 0.7f;
        frame.fog[1] = scene.farPlane;
        frame.fog[2] = scene.columns ? 1.0f : 0.0f;
        frame.fog[3] = 0.0f;

        BlurUniforms blur;
        blur.params[0] = 1.0f / static_cast<float>(kMomentSize);
        blur.params[1] = 0.0f;
        blur.params[2] = static_cast<float>(radius);
        blur.params[3] = 0.0f;
        memcpy(uniforms.data() + kRangeLight * stride, &lightViewProjection,
                sizeof(lightViewProjection));
        memcpy(uniforms.data() + kRangeFrame * stride, &frame, sizeof(frame));
        memcpy(uniforms.data() + kRangeBlurHorizontal * stride, &blur, sizeof(blur));
        blur.params[0] = 0.0f;
        blur.params[1] = 1.0f / static_cast<float>(kMomentSize);
        memcpy(uniforms.data() + kRangeBlurVertical * stride, &blur, sizeof(blur));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kRangeCount);

        driver->beginRenderPass(momentPass);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeLight * stride, sizeof(Math::Mat4));
        drawVisible(driver, scene, true, white, sampler, lightViewProjection);
        driver->endRenderPass();

        driver->beginRenderPass(blurHorizontal);
        driver->bindPipeline(blurPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeBlurHorizontal * stride,
                sizeof(BlurUniforms));
        driver->bindTexture(0, momentsA, momentSampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(blurVertical);
        driver->bindPipeline(blurPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeBlurVertical * stride,
                sizeof(BlurUniforms));
        driver->bindTexture(0, momentsB, momentSampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(pass);
        driver->bindUniformBuffer(0, uniformBuffer, kRangeFrame * stride, sizeof(FrameUniforms));
        driver->bindTexture(1, momentsA, momentSampler);
        drawVisible(driver, scene, false, white, sampler, frame.viewProjection);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    for (int i = 0; i < 2; ++i) destroyScene(driver, &scenes[i]);
    driver->destroy(blurPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(momentSampler);
    driver->destroy(momentDepth);
    driver->destroy(momentsB);
    driver->destroy(momentsA);
    driver->destroy(sampler);
    driver->destroy(white);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
