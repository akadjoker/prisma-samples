#include "common/Projection.h"
#include "common/SdkAnimation.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ground.frag.h"
#include "ground.vert.h"
#include "soldier.frag.h"
#include "soldier.vert.h"

namespace
{

const unsigned kMaxBones = 128;
const unsigned kSoldiers = 5;
const float kSpacing = 1.8f;
const float kHeight = 2.0f;
const float kGroundSize = 12.0f;

struct SkinHeader
{
    Math::Mat4 viewProjection;
    Math::Mat4 model;
    float lightDirection[4];
};

struct GroundUniforms
{
    Math::Mat4 viewProjection;
    float color[4];
};

static_assert(sizeof(Math::Mat4) == 64, "mat4 size");
static_assert(sizeof(SkinHeader) % 16 == 0, "bones must start on a 16 byte boundary");

const std::uint32_t kSkinBlockSize = sizeof(SkinHeader) + kMaxBones * 64;

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void measure(const zenapp::SdkMesh& mesh, Math::Vec3* low, Math::Vec3* high)
{
    const zenapp::SdkMeshData& data = mesh.data();
    *low = Math::Vec3(1e30f, 1e30f, 1e30f);
    *high = Math::Vec3(-1e30f, -1e30f, -1e30f);
    for (size_t m = 0; m < data.meshes.size(); ++m)
    {
        const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[data.meshes[m].vertexBuffers[0]];
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

    PlatformWindow* window = zenapp::openWindow("prisma 15 soldier", driverType);
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

    char path[1024];
    zenapp::mediaPath("Soldier/soldier.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh mesh;
    bool ready = mesh.load(driver, path) && mesh.meshCount() > 0;
    if (!ready) log_error("soldier: cannot load %s", path);

    zenapp::SdkAnimation animation;
    if (ready)
    {
        zenapp::mediaPath("Soldier/soldier.sdkmesh_anim", path, sizeof(path));
        ready = animation.load(path) && animation.bind(mesh.data());
        if (!ready) log_error("soldier: cannot load %s", path);
    }

    const unsigned meshCount = ready ? mesh.meshCount() : 0;
    for (unsigned m = 0; m < meshCount; ++m)
    {
        if (animation.influenceCount(mesh.data(), m) > kMaxBones)
        {
            log_error("soldier: mesh %u has more than %u influences", m, kMaxBones);
            ready = false;
        }
    }

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc textureDesc;
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.data = whitePixel;
    textureDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(textureDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "soldier sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const float s = kGroundSize;
    const float groundVertices[18] = { -s, 0, -s, -s, 0, s, s, 0, s, -s, 0, -s, s, 0, s, s, 0, -s };
    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(groundVertices);
    bufferDesc.data = groundVertices;
    bufferDesc.debugName = "ground vertices";
    const prisma::BufferHandle groundBuffer = driver->createBuffer(bufferDesc);

    const std::uint32_t stride =
            alignUp(kSkinBlockSize, driver->caps().uniformBufferOffsetAlignment);
    const std::uint32_t rangeCount = kSoldiers * meshCount + 1;
    const std::uint32_t groundRange = kSoldiers * meshCount;
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * rangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * rangeCount;
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "soldier uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle soldierVertex = zenapp::createShader(driver, soldier_vert);
    const prisma::ShaderHandle soldierFragment = zenapp::createShader(driver, soldier_frag);
    const prisma::ShaderHandle groundVertex = zenapp::createShader(driver, ground_vert);
    const prisma::ShaderHandle groundFragment = zenapp::createShader(driver, ground_frag);

    ct::Vector<prisma::PipelineHandle> pipelines;
    for (unsigned m = 0; m < meshCount; ++m)
    {
        prisma::PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = soldierVertex;
        pipelineDesc.fragmentShader = soldierFragment;
        ready = mesh.layout(m, &pipelineDesc) && ready;
        pipelineDesc.depthTest = true;
        pipelineDesc.cullMode = prisma::CullMode::Back;
        pipelineDesc.debugName = "soldier pipeline";
        pipelines.push_back(driver->createPipeline(pipelineDesc));
        ready = ready && pipelines[m].valid();
    }

    prisma::PipelineDesc groundDesc;
    groundDesc.vertexShader = groundVertex;
    groundDesc.fragmentShader = groundFragment;
    groundDesc.vertexBuffers[0].stride = sizeof(float) * 3;
    groundDesc.vertexBufferCount = 1;
    groundDesc.attributeCount = 1;
    groundDesc.attributes[0].location = 0;
    groundDesc.attributes[0].format = prisma::VertexFormat::Float3;
    groundDesc.attributes[0].offset = 0;
    groundDesc.depthTest = true;
    groundDesc.cullMode = prisma::CullMode::None;
    groundDesc.debugName = "ground pipeline";
    const prisma::PipelineHandle groundPipeline = driver->createPipeline(groundDesc);

    driver->destroy(soldierVertex);
    driver->destroy(soldierFragment);
    driver->destroy(groundVertex);
    driver->destroy(groundFragment);

    ready = ready && white.valid() && sampler.valid() && groundBuffer.valid() &&
            uniformBuffer.valid() && groundPipeline.valid();
    if (!ready) log_error("soldier: resource creation failed");

    Math::Vec3 low(0.0f, 0.0f, 0.0f);
    Math::Vec3 high(1.0f, 1.0f, 1.0f);
    if (ready) measure(mesh, &low, &high);
    const float extent = high.y - low.y;
    const float scale = extent > 0.0f ? kHeight / extent : 1.0f;
    const Math::Vec3 center = (low + high) * 0.5f;
    const Math::Mat4 fit = Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                           Math::Mat4::Translation(Math::Vec3(-center.x, -low.y, -center.z));
    const float duration = ready ? animation.duration() : 1.0f;
    const Math::Vec3 light = Math::Vec3(0.4f, 0.8f, 0.5f).Normalized();

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

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
        const float time = still ? 0.0f : static_cast<float>(time_seconds());

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -6.5f)) *
                                Math::Mat4::RotationX(0.25f) *
                                Math::Mat4::RotationY(sinf(time * 0.25f) * 0.5f) *
                                Math::Mat4::Translation(Math::Vec3(0.0f, -1.0f, 0.0f));
        const Math::Mat4 viewProjection = projection * view;

        for (unsigned soldier = 0; soldier < kSoldiers; ++soldier)
        {
            const float offset = duration * static_cast<float>(soldier) / kSoldiers;
            animation.evaluate(mesh.data(), fmodf(time + offset, duration));

            SkinHeader header;
            header.viewProjection = viewProjection;
            header.model = Math::Mat4::Translation(Math::Vec3(
                                   (static_cast<float>(soldier) - 2.0f) * kSpacing, 0.0f, 0.0f)) *
                           fit;
            header.lightDirection[0] = light.x;
            header.lightDirection[1] = light.y;
            header.lightDirection[2] = light.z;
            header.lightDirection[3] = 0.0f;

            for (unsigned m = 0; m < meshCount; ++m)
            {
                unsigned char* range = uniforms.data() + (soldier * meshCount + m) * stride;
                float bones[kMaxBones * 16];
                animation.influenceMatrices(mesh.data(), m, bones);
                memcpy(range, &header, sizeof(header));
                memcpy(range + sizeof(header), bones,
                        animation.influenceCount(mesh.data(), m) * sizeof(float) * 16);
            }
        }

        GroundUniforms ground;
        ground.viewProjection = viewProjection;
        ground.color[0] = 0.30f;
        ground.color[1] = 0.30f;
        ground.color[2] = 0.32f;
        ground.color[3] = 1.0f;
        memcpy(uniforms.data() + groundRange * stride, &ground, sizeof(ground));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * rangeCount);
        driver->beginRenderPass(pass);
        driver->bindPipeline(groundPipeline);
        driver->bindVertexBuffer(0, groundBuffer, 0);
        driver->bindUniformBuffer(0, uniformBuffer, groundRange * stride, sizeof(GroundUniforms));
        driver->draw(6, 0);
        for (unsigned soldier = 0; soldier < kSoldiers; ++soldier)
        {
            for (unsigned m = 0; m < meshCount; ++m)
            {
                driver->bindPipeline(pipelines[m]);
                driver->bindUniformBuffer(0, uniformBuffer, (soldier * meshCount + m) * stride,
                        kSkinBlockSize);
                for (unsigned i = 0; i < mesh.subsetCount(m); ++i)
                {
                    const zenapp::SdkSubset& subset = mesh.subset(m, i);
                    prisma::TextureHandle diffuse = mesh.diffuse(subset.materialId);
                    if (!diffuse.valid()) diffuse = white;
                    driver->bindTexture(0, diffuse, sampler);
                    mesh.drawSubset(driver, m, i);
                }
            }
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    for (size_t m = 0; m < pipelines.size(); ++m) driver->destroy(pipelines[m]);
    driver->destroy(groundPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(groundBuffer);
    driver->destroy(sampler);
    driver->destroy(white);
    mesh.destroy(driver);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
