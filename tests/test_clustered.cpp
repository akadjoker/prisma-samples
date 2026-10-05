#include "Check.h"
#include "GpuContext.h"
#include "Lights.h"
#include "SceneHelpers.h"
#include "prisma/rhi/ShaderBlob.h"

#include "clustered_probe.frag.h"
#include "no_buffer.vert.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

struct Probe
{
    float mode[4];
    float a[4];
    float b[4];
    float c[4];
    float d[4];
    float e[4];
};

zenapp::ClusteredBuffers buffers;

} // namespace

int main(int argc, char** argv)
{
    using namespace prisma;

    GpuContext gpu;
    if (!gpu.open(argc, argv)) return 1;
    Driver* driver = gpu.driver;
    CHECK(driver->caps().uniformBufferOffsetAlignment <= 256);

    const scene::Scene s = scene::makeScene(1280, 720);
    zenapp::Froxelizer froxelizer;
    froxelizer.setDepthRange(s.zLightNear, s.zLightFar);
    froxelizer.prepare(s.width, s.height, s.projection, s.nearPlane, s.farPlane);

    zenapp::LightSet set;
    zenapp::clearLights(&set);
    const float sunDirection[3] = { 0.3f, 0.9f, 0.4f };
    const float sunColor[3] = { 1.0f, 0.9f, 0.8f };
    zenapp::setSunLight(&set, sunDirection, sunColor, 1.5f);
    for (unsigned i = 0; i < 150; ++i)
    {
        float position[3];
        scene::worldFromClip(s, scene::randomRange(-1.1f, 1.1f), scene::randomRange(-1.1f, 1.1f),
                scene::randomRange(1.0f, 60.0f), position);
        const float color[3] = { scene::randomRange(0.2f, 1.0f), scene::randomRange(0.2f, 1.0f),
            scene::randomRange(0.2f, 1.0f) };
        const float radius = scene::randomRange(2.0f, 9.0f);
        if (scene::random01() < 0.4f)
        {
            float direction[3] = { scene::randomRange(-1.0f, 1.0f), scene::randomRange(-1.0f, 0.2f),
                scene::randomRange(-1.0f, 1.0f) };
            scene::normalize(direction);
            zenapp::addSpotLight(&set, position, direction, color, scene::randomRange(20.0f, 60.0f),
                    radius, 0.15f, scene::randomRange(0.3f, 0.8f));
        }
        else
            zenapp::addPointLight(&set, position, color, scene::randomRange(10.0f, 40.0f), radius);
    }
    CHECK(set.count == 150);
    zenapp::buildClusteredBuffers(set, froxelizer, s.view, &buffers);
    printf("records overflowed: %d\n", froxelizer.recordsOverflowed());

    TextureDesc targetDesc;
    targetDesc.width = 4;
    targetDesc.height = 4;
    targetDesc.usage = kTextureSampled | kTextureRenderTarget;
    const TextureHandle target = driver->createTexture(targetDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned probeStride = (sizeof(Probe) + alignment - 1) / alignment * alignment;
    BufferDesc probeBufferDesc;
    probeBufferDesc.usage = BufferUsage::Uniform;
    probeBufferDesc.size = probeStride;
    probeBufferDesc.update = BufferUpdate::Stream;
    const BufferHandle probeBuffer = driver->createBuffer(probeBufferDesc);
    BufferDesc blocksDesc;
    blocksDesc.usage = BufferUsage::Uniform;
    blocksDesc.size = sizeof(buffers);
    blocksDesc.data = &buffers;
    const BufferHandle blocks = driver->createBuffer(blocksDesc);

    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(clustered_probe_frag, driver->caps());
    const ShaderHandle vertexShader = driver->createShader(vertexDesc);
    const ShaderHandle fragmentShader = driver->createShader(fragmentDesc);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 1;
    pipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
    const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);
    CHECK(target.valid() && probeBuffer.valid() && blocks.valid() && pipeline.valid());
    if (!(target.valid() && probeBuffer.valid() && blocks.valid() && pipeline.valid()))
    {
        gpu.close();
        return 1;
    }

    const auto run = [&](const Probe& probe) {
        driver->beginFrame();
        driver->updateBuffer(probeBuffer, 0, &probe, sizeof(probe));
        RenderPassDesc pass;
        pass.colors[0].texture = target;
        pass.colorCount = 1;
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindUniformBuffer(0, probeBuffer, 0, sizeof(Probe));
        driver->bindUniformBuffer(3, blocks, offsetof(zenapp::ClusteredBuffers, cluster),
                sizeof(zenapp::ClusterUniforms));
        driver->bindUniformBuffer(4, blocks, offsetof(zenapp::ClusteredBuffers, lights),
                sizeof(buffers.lights));
        driver->bindUniformBuffer(5, blocks, offsetof(zenapp::ClusteredBuffers, froxels),
                sizeof(buffers.froxels));
        driver->bindUniformBuffer(6, blocks, offsetof(zenapp::ClusteredBuffers, records),
                sizeof(buffers.records));
        driver->draw(3, 0);
        driver->endRenderPass();
        unsigned char pixel[4] = { 0, 0, 0, 0 };
        Rect rect;
        rect.x = 1;
        rect.y = 1;
        rect.width = 1;
        rect.height = 1;
        RenderTarget renderTarget;
        renderTarget.texture = target;
        CHECK(driver->readPixels(renderTarget, rect, pixel));
        driver->endFrame();
        driver->present();
        return (pixel[0] / 255.0f + pixel[1] / 65025.0f + pixel[2] / 16581375.0f +
                       pixel[3] / 4228250625.0f) * probe.mode[2];
    };

    unsigned indexMismatches = 0;
    unsigned lit = 0;
    float worst = 0.0f;
    const unsigned probes = 120;
    for (unsigned i = 0; i < probes; ++i)
    {
        const float ndcX = scene::randomRange(-1.0f, 1.0f);
        const float ndcY = scene::randomRange(-1.0f, 1.0f);
        const float depth = scene::randomRange(0.3f, s.zLightFar * 0.95f);
        float position[3];
        scene::worldFromClip(s, ndcX, ndcY, depth, position);
        float normal[3] = { scene::randomRange(-1.0f, 1.0f), scene::randomRange(-1.0f, 1.0f),
            scene::randomRange(-1.0f, 1.0f) };
        scene::normalize(normal);
        float view[3] = { s.eye[0] - position[0], s.eye[1] - position[1], s.eye[2] - position[2] };
        scene::normalize(view);

        Probe probe;
        memset(&probe, 0, sizeof(probe));
        probe.mode[2] = 4096.0f;
        for (int k = 0; k < 3; ++k)
        {
            probe.a[k] = normal[k];
            probe.b[k] = view[k];
            probe.c[k] = position[k];
            probe.d[k] = scene::randomRange(0.1f, 1.0f);
        }
        probe.a[3] = scene::random01() < 0.5f ? 0.0f : 1.0f;
        probe.b[3] = scene::randomRange(0.15f, 1.0f);
        probe.e[0] = ndcX * depth;
        probe.e[1] = ndcY * depth;
        probe.e[2] = depth;

        probe.mode[0] = 2.0f;
        const float gpuIndex = run(probe);
        if (static_cast<unsigned>(gpuIndex + 0.5f) != froxelizer.froxelIndexFor(ndcX, ndcY, depth))
            ++indexMismatches;

        probe.mode[0] = 0.0f;
        const float clustered = run(probe);
        probe.mode[0] = 1.0f;
        const float brute = run(probe);
        if (brute > 0.05f) ++lit;
        const float error = fabsf(clustered - brute) / fmaxf(brute, 0.05f);
        worst = error > worst ? error : worst;
    }
    printf("%u probes: %u froxel index mismatches, %u lit, worst clustered vs brute force error %.5f\n",
            probes, indexMismatches, lit, worst);
    CHECK(indexMismatches == 0);
    CHECK(lit > probes / 4);
    CHECK(worst < 1e-3f);

    driver->destroy(pipeline);
    driver->destroy(blocks);
    driver->destroy(probeBuffer);
    driver->destroy(target);
    gpu.close();

    printf(failures ? "test_clustered: %d failures\n" : "test_clustered: all passed\n", failures);
    return failures ? 1 : 0;
}
