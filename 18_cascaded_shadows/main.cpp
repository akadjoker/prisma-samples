#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cascade_depth.frag.h"
#include "cascade_depth.vert.h"
#include "cascade_scene.frag.h"
#include "cascade_scene.vert.h"

namespace
{

const std::uint32_t kShadowSize = 2048;
const std::uint32_t kCascadeCount = 4;
const float kSplitLambda = 0.7f;

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 view;
    Math::Mat4 lightViewProjection[kCascadeCount];
    float lightDirection[4];
    float splits[4];
    float texels[4];
    float params[4];
    float extra[4];
};

struct Cascades
{
    Math::Mat4 viewProjection[kCascadeCount];
    float splits[kCascadeCount];
    float texels[kCascadeCount];
};

struct Scene
{
    zenapp::SdkMesh mesh;
    bool loaded = false;
    bool columns = false;
    ct::Vector<Math::Box> bounds;
    ct::Vector<unsigned> layoutOf;
    ct::Vector<prisma::PipelineHandle> depthPipelines;
    ct::Vector<prisma::PipelineHandle> scenePipelines;
    Math::Box total;
    Math::Vec3 toLight;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float casterDistance = 100.0f;
};

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

Math::Mat4 orthographicZeroToOne(float half, float depth)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Ortho(-half, half, -half, half, 0.0f, depth);
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
        log_error("cascaded shadows: cannot load %s", path);
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
        prisma::PipelineDesc depthDesc;
        depthDesc.vertexShader = shaders[0];
        depthDesc.fragmentShader = shaders[1];
        if (!mesh.layout(representatives[l], &depthDesc)) return false;
        depthDesc.depthTest = true;
        depthDesc.cullMode = prisma::CullMode::Back;
        depthDesc.depthBiasConstant = 2.0f;
        depthDesc.depthBiasSlope = 2.0f;
        depthDesc.targets.window = false;
        depthDesc.targets.colorCount = 0;
        depthDesc.targets.depth = prisma::TextureFormat::Depth32F;
        depthDesc.debugName = "cascade depth pipeline";
        scene->depthPipelines.push_back(driver->createPipeline(depthDesc));

        prisma::PipelineDesc sceneDesc;
        sceneDesc.vertexShader = shaders[2];
        sceneDesc.fragmentShader = shaders[3];
        if (!mesh.layout(representatives[l], &sceneDesc)) return false;
        sceneDesc.depthTest = true;
        sceneDesc.cullMode = prisma::CullMode::Back;
        sceneDesc.debugName = "cascade scene pipeline";
        scene->scenePipelines.push_back(driver->createPipeline(sceneDesc));

        if (!scene->depthPipelines[l].valid() || !scene->scenePipelines[l].valid()) return false;
    }

    const Math::Vec3 size = high - low;
    if (columns)
    {
        scene->nearPlane = 0.1f;
        scene->farPlane = 80.0f;
        scene->toLight = Math::Vec3(0.6f, 0.55f, -0.4f).Normalized();
        scene->casterDistance = 80.0f;
    }
    else
    {
        const float radius = size.Length() * 0.5f;
        scene->nearPlane = radius * 0.001f > 0.1f ? radius * 0.001f : 0.1f;
        scene->farPlane = radius * 0.9f;
        if (!findLight(mesh.data(), &scene->toLight))
            scene->toLight = Math::Vec3(0.5f, 0.7f, 0.3f).Normalized();
        scene->casterDistance = size.y / scene->toLight.y;
    }
    scene->loaded = true;
    return true;
}

void destroyScene(prisma::Driver* driver, Scene* scene)
{
    for (size_t i = 0; i < scene->scenePipelines.size(); ++i)
        driver->destroy(scene->scenePipelines[i]);
    for (size_t i = 0; i < scene->depthPipelines.size(); ++i)
        driver->destroy(scene->depthPipelines[i]);
    scene->mesh.destroy(driver);
}

void computeCascades(const Math::Mat4& view, const Math::Mat4& projection, const Scene& scene,
        Cascades* out)
{
    const float nearPlane = scene.nearPlane;
    const float farPlane = scene.farPlane;
    const Math::Mat4 inverse = (projection * view).Inverse();
    Math::Vec3 nearCorners[4];
    Math::Vec3 farCorners[4];
    for (int i = 0; i < 4; ++i)
    {
        const float x = (i & 1) ? 1.0f : -1.0f;
        const float y = (i & 2) ? 1.0f : -1.0f;
        nearCorners[i] = inverse.TransformPointPerspective(Math::Vec3(x, y, 0.0f));
        farCorners[i] = inverse.TransformPointPerspective(Math::Vec3(x, y, 1.0f));
    }

    float edges[kCascadeCount + 1];
    edges[0] = nearPlane;
    for (std::uint32_t i = 1; i <= kCascadeCount; ++i)
    {
        const float fraction = static_cast<float>(i) / static_cast<float>(kCascadeCount);
        const float logarithmic = nearPlane * powf(farPlane / nearPlane, fraction);
        const float uniform = nearPlane + (farPlane - nearPlane) * fraction;
        edges[i] = kSplitLambda * logarithmic + (1.0f - kSplitLambda) * uniform;
    }

    const Math::Vec3 toLight = scene.toLight;
    const Math::Vec3 forward = toLight * -1.0f;
    const Math::Vec3 up(0.0f, 1.0f, 0.0f);
    const Math::Vec3 right = forward.Cross(up).Normalized();
    const Math::Vec3 lightUp = right.Cross(forward);

    for (std::uint32_t c = 0; c < kCascadeCount; ++c)
    {
        const float t0 = (edges[c] - nearPlane) / (farPlane - nearPlane);
        const float t1 = (edges[c + 1] - nearPlane) / (farPlane - nearPlane);
        Math::Vec3 corners[8];
        Math::Vec3 center(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            const Math::Vec3 ray = farCorners[i] - nearCorners[i];
            corners[i] = nearCorners[i] + ray * t0;
            corners[i + 4] = nearCorners[i] + ray * t1;
        }
        for (int i = 0; i < 8; ++i) center = center + corners[i];
        center = center * (1.0f / 8.0f);

        float radius = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float distance = (corners[i] - center).Length();
            if (distance > radius) radius = distance;
        }
        radius = ceilf(radius * 16.0f) / 16.0f;

        const float texel = 2.0f * radius / static_cast<float>(kShadowSize);
        const float x = floorf(right.Dot(center) / texel) * texel;
        const float y = floorf(lightUp.Dot(center) / texel) * texel;
        const float z = forward.Dot(center);
        const Math::Vec3 snapped = right * x + lightUp * y + forward * z;

        const Math::Mat4 lightView = Math::Mat4::LookAt(
                snapped + toLight * (radius + scene.casterDistance), snapped, up);
        out->viewProjection[c] =
                orthographicZeroToOne(radius, 2.0f * radius + scene.casterDistance) * lightView;
        out->splits[c] = edges[c + 1];
        out->texels[c] = texel;
    }
}

void drawVisible(prisma::Driver* driver, const Scene& scene, bool depthOnly,
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
                    depthOnly ? scene.depthPipelines[current] : scene.scenePipelines[current]);
        }
        for (unsigned i = 0; i < scene.mesh.subsetCount(m); ++i)
        {
            if (!depthOnly)
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

    PlatformWindow* window = zenapp::openWindow("prisma 18 cascaded shadows", driverType);
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
        log_error("cascaded shadows: this GPU cannot create a %ux%u texture", kShadowSize,
                kShadowSize);
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

    prisma::TextureDesc shadowDesc;
    shadowDesc.type = prisma::TextureType::Texture2DArray;
    shadowDesc.format = prisma::TextureFormat::Depth32F;
    shadowDesc.width = kShadowSize;
    shadowDesc.height = kShadowSize;
    shadowDesc.depth = kCascadeCount;
    shadowDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    shadowDesc.debugName = "cascade maps";
    const prisma::TextureHandle shadowMap = driver->createTexture(shadowDesc);

    prisma::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.minFilter = prisma::Filter::Linear;
    shadowSamplerDesc.magFilter = prisma::Filter::Linear;
    shadowSamplerDesc.mipFilter = prisma::MipFilter::None;
    shadowSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.compare = true;
    shadowSamplerDesc.compareOp = prisma::CompareOp::LessEqual;
    shadowSamplerDesc.debugName = "cascade sampler";
    const prisma::SamplerHandle shadowSampler = driver->createSampler(shadowSamplerDesc);

    const std::uint32_t kFrameRange = kCascadeCount;
    const std::uint32_t kRangeCount = kCascadeCount + 1;
    const std::uint32_t stride =
            alignUp(sizeof(FrameUniforms), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * kRangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * kRangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "cascade uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    prisma::ShaderHandle shaders[4];
    shaders[0] = zenapp::createShader(driver, cascade_depth_vert);
    shaders[1] = zenapp::createShader(driver, cascade_depth_frag);
    shaders[2] = zenapp::createShader(driver, cascade_scene_vert);
    shaders[3] = zenapp::createShader(driver, cascade_scene_frag);

    Scene scenes[2];
    bool ready = white.valid() && sampler.valid() && shadowMap.valid() && shadowSampler.valid() &&
                 uniformBuffer.valid();
    if (ready)
        ready = buildScene(driver, "powerplant/powerplant.sdkmesh", false, shaders, &scenes[0]);
    if (ready) buildScene(driver, "ShadowColumns/testscene.sdkmesh", true, shaders, &scenes[1]);
    if (!ready) log_error("cascaded shadows: resource creation failed");

    for (int i = 0; i < 4; ++i) driver->destroy(shaders[i]);

    prisma::RenderPassDesc shadowPasses[kCascadeCount];
    for (std::uint32_t c = 0; c < kCascadeCount; ++c)
    {
        shadowPasses[c].depth.texture = shadowMap;
        shadowPasses[c].depth.layer = c;
    }

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.55f;
    pass.clearColor[1] = 0.68f;
    pass.clearColor[2] = 0.85f;

    int active = zenapp::hasArgument(argc, argv, "columns") && scenes[1].loaded ? 1 : 0;
    bool tint = zenapp::hasArgument(argc, argv, "tint");
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_C)) tint = !tint;
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

        Cascades cascades;
        computeCascades(view, projection, scene, &cascades);

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.view = view;
        for (std::uint32_t c = 0; c < kCascadeCount; ++c)
        {
            frame.lightViewProjection[c] = cascades.viewProjection[c];
            frame.splits[c] = cascades.splits[c];
            frame.texels[c] = cascades.texels[c];
            memcpy(uniforms.data() + c * stride, &cascades.viewProjection[c], sizeof(Math::Mat4));
        }
        frame.lightDirection[0] = scene.toLight.x;
        frame.lightDirection[1] = scene.toLight.y;
        frame.lightDirection[2] = scene.toLight.z;
        frame.lightDirection[3] = 0.0f;
        frame.params[0] = tint ? 1.0f : 0.0f;
        frame.params[1] = 1.5f;
        frame.params[2] = scene.farPlane * 0.7f;
        frame.params[3] = scene.farPlane;
        frame.extra[0] = scene.columns ? 1.0f : 0.0f;
        frame.extra[1] = scene.nearPlane;
        frame.extra[2] = frame.extra[3] = 0.0f;
        memcpy(uniforms.data() + kFrameRange * stride, &frame, sizeof(frame));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * kRangeCount);

        for (std::uint32_t c = 0; c < kCascadeCount; ++c)
        {
            driver->beginRenderPass(shadowPasses[c]);
            driver->bindUniformBuffer(0, uniformBuffer, c * stride, sizeof(Math::Mat4));
            drawVisible(driver, scene, true, white, sampler, cascades.viewProjection[c]);
            driver->endRenderPass();
        }

        driver->beginRenderPass(pass);
        driver->bindUniformBuffer(0, uniformBuffer, kFrameRange * stride, sizeof(FrameUniforms));
        driver->bindTexture(1, shadowMap, shadowSampler);
        drawVisible(driver, scene, false, white, sampler, frame.viewProjection);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    for (int i = 0; i < 2; ++i) destroyScene(driver, &scenes[i]);
    driver->destroy(uniformBuffer);
    driver->destroy(shadowSampler);
    driver->destroy(shadowMap);
    driver->destroy(sampler);
    driver->destroy(white);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
