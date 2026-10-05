#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pn.frag.h"
#include "pn.tesc.h"
#include "pn.tese.h"
#include "pn.vert.h"
#include "pn_flat.vert.h"
#include "pn_wire.frag.h"

namespace
{

const int kMinLevel = 1;
const int kMaxLevel = 16;

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 model;
    float camera[4];
    float lightDirection[4];
    float levels[4];
};

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

    PlatformWindow* window = zenapp::openWindow("prisma 21 pn triangles", driverType);
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

    if (!driver->caps().tessellation || driver->caps().maxPatchControlPoints < 3)
    {
        log_error("pn triangles: this GPU has no tessellation");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("Tiger/tiger.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh mesh;
    bool ready = mesh.load(driver, path) && mesh.meshCount() > 0;
    if (!ready)
    {
        log_error("pn triangles: cannot load %s", path);
        mesh.destroy(driver);
        mesh = zenapp::SdkMesh();
        zenapp::mediaPath("Teapot/Teapot.sdkmesh", path, sizeof(path));
        ready = mesh.load(driver, path) && mesh.meshCount() > 0;
        if (!ready) log_error("pn triangles: cannot load %s", path);
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
    samplerDesc.debugName = "model sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, pn_vert);
    const prisma::ShaderHandle controlShader = zenapp::createShader(driver, pn_tesc);
    const prisma::ShaderHandle evaluationShader = zenapp::createShader(driver, pn_tese);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, pn_frag);
    const prisma::ShaderHandle flatShader = zenapp::createShader(driver, pn_flat_vert);
    const prisma::ShaderHandle wireShader = zenapp::createShader(driver, pn_wire_frag);

    prisma::PipelineDesc curvedDesc;
    curvedDesc.vertexShader = vertexShader;
    curvedDesc.tessControlShader = controlShader;
    curvedDesc.tessEvalShader = evaluationShader;
    curvedDesc.fragmentShader = fragmentShader;
    curvedDesc.topology = prisma::Topology::Patches;
    curvedDesc.patchControlPoints = 3;
    if (ready) ready = mesh.layout(0, &curvedDesc);
    curvedDesc.depthTest = true;
    curvedDesc.cullMode = prisma::CullMode::None;
    curvedDesc.depthBiasConstant = 2.0f;
    curvedDesc.depthBiasSlope = 1.5f;
    curvedDesc.debugName = "curved pipeline";
    const prisma::PipelineHandle curvedPipeline = driver->createPipeline(curvedDesc);

    prisma::PipelineDesc flatDesc = curvedDesc;
    flatDesc.vertexShader = flatShader;
    flatDesc.tessControlShader = prisma::ShaderHandle();
    flatDesc.tessEvalShader = prisma::ShaderHandle();
    flatDesc.topology = prisma::Topology::Triangles;
    flatDesc.patchControlPoints = 0;
    flatDesc.cullMode = prisma::CullMode::Back;
    flatDesc.debugName = "flat pipeline";
    const prisma::PipelineHandle flatPipeline = driver->createPipeline(flatDesc);

    const bool wireSupported = driver->caps().wireframe;
    prisma::PipelineHandle curvedWirePipeline;
    prisma::PipelineHandle flatWirePipeline;
    if (wireSupported)
    {
        prisma::PipelineDesc wireDesc = curvedDesc;
        wireDesc.fragmentShader = wireShader;
        wireDesc.wireframe = true;
        wireDesc.depthCompare = prisma::CompareOp::LessEqual;
        wireDesc.depthBiasConstant = 0.0f;
        wireDesc.depthBiasSlope = 0.0f;
        wireDesc.debugName = "curved wire pipeline";
        curvedWirePipeline = driver->createPipeline(wireDesc);

        wireDesc = flatDesc;
        wireDesc.fragmentShader = wireShader;
        wireDesc.wireframe = true;
        wireDesc.depthCompare = prisma::CompareOp::LessEqual;
        wireDesc.depthBiasConstant = 0.0f;
        wireDesc.depthBiasSlope = 0.0f;
        wireDesc.debugName = "flat wire pipeline";
        flatWirePipeline = driver->createPipeline(wireDesc);
    }

    driver->destroy(vertexShader);
    driver->destroy(controlShader);
    driver->destroy(evaluationShader);
    driver->destroy(fragmentShader);
    driver->destroy(flatShader);
    driver->destroy(wireShader);

    ready = ready && white.valid() && sampler.valid() && uniformBuffer.valid() &&
            curvedPipeline.valid() && flatPipeline.valid() &&
            (!wireSupported || (curvedWirePipeline.valid() && flatWirePipeline.valid()));
    if (!ready) log_error("pn triangles: resource creation failed");

    Math::Vec3 center(0.0f, 0.0f, 0.0f);
    float size = 1.0f;
    if (ready)
    {
        const zenapp::SdkMeshHeader& part = mesh.data().meshes[0];
        center = Math::Vec3(part.boundingBoxCenter[0], part.boundingBoxCenter[1],
                part.boundingBoxCenter[2]);
        const float x = part.boundingBoxExtents[0];
        const float y = part.boundingBoxExtents[1];
        const float z = part.boundingBoxExtents[2];
        size = x > y ? (x > z ? x : z) : (y > z ? y : z);
        if (size <= 0.0f) size = 1.0f;
    }
    const float scale = 2.4f / size;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int level = 4;
    bool wire = wireSupported && zenapp::hasArgument(argc, argv, "wire");
    bool curved = !zenapp::hasArgument(argc, argv, "flat");
    const Math::Vec3 light = Math::Vec3(0.4f, 0.7f, 0.6f).Normalized();

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_UP) && level < kMaxLevel)
        {
            ++level;
            printf("tessellation level %d\n", level);
        }
        if (key_pressed(window, KEY_DOWN) && level > kMinLevel)
        {
            --level;
            printf("tessellation level %d\n", level);
        }
        if (key_pressed(window, KEY_W) && wireSupported) wire = !wire;
        if (key_pressed(window, KEY_P)) curved = !curved;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;
        const float distance = 4.5f + 1.5f * sinf(time * 0.35f);

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.9f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -distance));

        FrameUniforms frame;
        frame.model = Math::Mat4::RotationX(0.25f) * Math::Mat4::RotationY(time * 0.4f + 1.4f) *
                      Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                      Math::Mat4::Translation(-center);
        frame.viewProjection = projection * view;
        frame.camera[0] = 0.0f;
        frame.camera[1] = 0.0f;
        frame.camera[2] = distance;
        frame.camera[3] = 1.0f;
        frame.lightDirection[0] = light.x;
        frame.lightDirection[1] = light.y;
        frame.lightDirection[2] = light.z;
        frame.lightDirection[3] = 0.0f;
        frame.levels[0] = static_cast<float>(level);
        frame.levels[1] = 4.5f;
        frame.levels[2] = static_cast<float>(kMaxLevel);
        frame.levels[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);
        for (int layer = 0; layer < (wire ? 2 : 1); ++layer)
        {
            if (layer == 0) driver->bindPipeline(curved ? curvedPipeline : flatPipeline);
            else
                driver->bindPipeline(curved ? curvedWirePipeline : flatWirePipeline);
            driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
            for (unsigned i = 0; i < mesh.subsetCount(0); ++i)
            {
                const zenapp::SdkSubset& subset = mesh.subset(0, i);
                prisma::TextureHandle diffuse = mesh.diffuse(subset.materialId);
                if (!diffuse.valid()) diffuse = white;
                driver->bindTexture(0, diffuse, sampler);
                mesh.drawSubset(driver, 0, i);
            }
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (curvedWirePipeline.valid()) driver->destroy(curvedWirePipeline);
    if (flatWirePipeline.valid()) driver->destroy(flatWirePipeline);
    if (curvedPipeline.valid()) driver->destroy(curvedPipeline);
    if (flatPipeline.valid()) driver->destroy(flatPipeline);
    if (uniformBuffer.valid()) driver->destroy(uniformBuffer);
    if (sampler.valid()) driver->destroy(sampler);
    if (white.valid()) driver->destroy(white);
    mesh.destroy(driver);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
