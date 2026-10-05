#include "common/ColorPyramid.h"
#include "common/Equirect.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/ObjModel.h"
#include "common/PostProcess.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "refraction.frag.h"
#include "refraction.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"

namespace
{

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 inverseViewProjection;
    float camera[4];
    float exposure[4];
    float sunDirection[4];
    float sunColorIntensity[4];
};

struct ObjectUniforms
{
    Math::Mat4 model;
    Math::Mat4 normalMatrix;
};

struct RefractionUniforms
{
    float baseIor[4];
    float absorptionThickness[4];
    float params[4];
};

const unsigned kRenderWidth = 1280;
const unsigned kRenderHeight = 720;
const float kFov = 0.7853982f;
const float kIblLuminance = 40000.0f;
const float kAperture = 16.0f;
const float kShutter = 1.0f / 125.0f;
const float kSensitivity = 100.0f;

float exposureFactor()
{
    const float ev100 = log2f(kAperture * kAperture / kShutter * 100.0f / kSensitivity);
    return 1.0f / (1.2f * powf(2.0f, ev100));
}

float numberArgument(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

void setTargetFormats(prisma::PipelineDesc* desc, bool depth)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = prisma::TextureFormat::RGBA16F;
    desc->targets.depth = depth ? prisma::TextureFormat::Depth32F : prisma::TextureFormat::None;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const float blur = numberArgument(argc, argv, "blur", 1.0f);
    const float ev = numberArgument(argc, argv, "ev", 1.2f);
    const float yaw = numberArgument(argc, argv, "yaw", 0.4f);
    const float swing = numberArgument(argc, argv, "swing", 0.25f);
    const float distanceScale = numberArgument(argc, argv, "distance", 2.1f);
    const float ior = numberArgument(argc, argv, "ior", 1.5f);
    const float thickness = numberArgument(argc, argv, "thickness", 1.0f);
    const float roughness = numberArgument(argc, argv, "roughness", 0.25f);
    const float transmission = numberArgument(argc, argv, "transmission", 1.0f);
    const float bloomStrength = numberArgument(argc, argv, "bloom", 0.10f);
    const bool fxaaEnabled = numberArgument(argc, argv, "fxaa", 1.0f) > 0.5f;

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "%s/../../assets/models/monkey/monkey.obj",
                PRISMA_MODELS_DIR);
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath), "%s/../environments/venetian_crossroads_2k.hdr",
                PRISMA_MODELS_DIR);

    zenapp::GltfModel model;
    if (!zenapp::loadObj(modelPath, &model, false))
    {
        log_error("refraction: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (size_t p = 0; p < model.primitives.size(); ++p)
    {
        for (int c = 0; c < 3; ++c)
        {
            const float lo = model.primitives[p].boundsMin[c];
            const float hi = model.primitives[p].boundsMax[c];
            if (c == 0) { low.x = fminf(low.x, lo); high.x = fmaxf(high.x, hi); }
            if (c == 1) { low.y = fminf(low.y, lo); high.y = fmaxf(high.y, hi); }
            if (c == 2) { low.z = fminf(low.z, lo); high.z = fmaxf(high.z, hi); }
        }
    }
    const Math::Vec3 center = (low + high) * 0.5f;
    const float radius = (high - low).Length() * 0.5f;

    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("refraction: cannot read %s", environmentPath);
        return 1;
    }

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 36 refraction", driverType);
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
        log_error("refraction: this GPU cannot render to float textures");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    const float exposure = exposureFactor() * powf(2.0f, ev);
    zenapp::Ibl ibl;
    const bool iblReady = zenapp::createIbl(driver, faces, &ibl, kIblLuminance * exposure);
    zenapp::GltfGpuOptions gpuOptions;
    zenapp::GltfGpu gpu;
    const bool gpuReady = zenapp::createGltfGpu(driver, model, gpuOptions, &gpu);

    zenapp::ColorPyramid pyramid;
    const bool pyramidReady = zenapp::createColorPyramid(driver, kRenderWidth, kRenderHeight, &pyramid);
    zenapp::PostProcess post;
    const bool postReady = zenapp::createPostProcess(driver, kRenderWidth, kRenderHeight,
            bloomStrength, fxaaEnabled, &post);

    prisma::TextureDesc colorDesc;
    colorDesc.format = prisma::TextureFormat::RGBA16F;
    colorDesc.width = kRenderWidth;
    colorDesc.height = kRenderHeight;
    colorDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    colorDesc.debugName = "scene color";
    const prisma::TextureHandle sceneColor = driver->createTexture(colorDesc);
    prisma::TextureDesc depthDesc;
    depthDesc.format = prisma::TextureFormat::Depth32F;
    depthDesc.width = kRenderWidth;
    depthDesc.height = kRenderHeight;
    depthDesc.usage = prisma::kTextureRenderTarget;
    depthDesc.debugName = "scene depth";
    const prisma::TextureHandle sceneDepth = driver->createTexture(depthDesc);

    prisma::BufferDesc frameDesc;
    frameDesc.usage = prisma::BufferUsage::Uniform;
    frameDesc.size = sizeof(FrameUniforms);
    frameDesc.update = prisma::BufferUpdate::Stream;
    frameDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(frameDesc);

    ObjectUniforms object;
    object.model = Math::Mat4::Translation(-center);
    object.normalMatrix = object.model.Inverse().Transposed();
    prisma::BufferDesc objectDesc;
    objectDesc.usage = prisma::BufferUsage::Uniform;
    objectDesc.size = sizeof(object);
    objectDesc.data = &object;
    objectDesc.debugName = "object uniforms";
    const prisma::BufferHandle objectBuffer = driver->createBuffer(objectDesc);

    RefractionUniforms refraction;
    memset(&refraction, 0, sizeof(refraction));
    refraction.baseIor[0] = refraction.baseIor[1] = refraction.baseIor[2] = 1.0f;
    refraction.baseIor[3] = ior;
    refraction.absorptionThickness[0] = numberArgument(argc, argv, "absorbr", 0.0f);
    refraction.absorptionThickness[1] = numberArgument(argc, argv, "absorbg", 0.7f);
    refraction.absorptionThickness[2] = numberArgument(argc, argv, "absorbb", 3.5f);
    refraction.absorptionThickness[3] = thickness;
    refraction.params[0] = roughness;
    refraction.params[1] = 0.0f;
    refraction.params[2] = transmission;
    refraction.params[3] = zenapp::colorPyramidLodOffset(kFov, kRenderHeight);
    prisma::BufferDesc refractionDesc;
    refractionDesc.usage = prisma::BufferUsage::Uniform;
    refractionDesc.size = sizeof(refraction);
    refractionDesc.data = &refraction;
    refractionDesc.debugName = "refraction uniforms";
    const prisma::BufferHandle refractionBuffer = driver->createBuffer(refractionDesc);

    const prisma::ShaderHandle refractionVertex = zenapp::createShader(driver, refraction_vert);
    const prisma::ShaderHandle refractionFragment = zenapp::createShader(driver, refraction_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc refractionPipelineDesc;
    refractionPipelineDesc.vertexShader = refractionVertex;
    refractionPipelineDesc.fragmentShader = refractionFragment;
    refractionPipelineDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
    refractionPipelineDesc.vertexBufferCount = 1;
    refractionPipelineDesc.attributeCount = 2;
    refractionPipelineDesc.attributes[0].location = 0;
    refractionPipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    refractionPipelineDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
    refractionPipelineDesc.attributes[1].location = 1;
    refractionPipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    refractionPipelineDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, normal);
    refractionPipelineDesc.depthTest = true;
    refractionPipelineDesc.cullMode = prisma::CullMode::Back;
    setTargetFormats(&refractionPipelineDesc, true);
    refractionPipelineDesc.debugName = "refraction";
    const prisma::PipelineHandle refractionPipeline = driver->createPipeline(refractionPipelineDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    setTargetFormats(&skyDesc, true);
    skyDesc.debugName = "sky";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(refractionVertex);
    driver->destroy(refractionFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool ready = iblReady && gpuReady && pyramidReady && postReady && sceneColor.valid() &&
                       sceneDepth.valid() && frameBuffer.valid() && objectBuffer.valid() &&
                       refractionBuffer.valid() && refractionPipeline.valid() && skyPipeline.valid();
    if (!ready) log_error("refraction: resource creation failed");

    prisma::RenderPassDesc skyPass;
    skyPass.colorCount = 1;
    skyPass.depth.texture = sceneDepth;
    skyPass.depthStore = prisma::StoreOp::Discard;
    skyPass.clearColor[0] = skyPass.clearColor[1] = skyPass.clearColor[2] = 0.0f;
    prisma::RenderPassDesc scenePass = skyPass;
    scenePass.colors[0].texture = sceneColor;
    skyPass.colors[0].texture = pyramid.level[0];
    prisma::RenderPassDesc windowPass;
    windowPass.depthLoad = prisma::LoadOp::DontCare;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;

        const float aspect = static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight);
        const float angle = yaw + (still ? 0.0f : sinf(static_cast<float>(time_seconds()) * 0.4f) * swing);
        const float distance = radius * distanceScale;
        const Math::Vec3 eye(distance * sinf(angle), radius * 0.15f, distance * cosf(angle));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(kFov, aspect, radius * 0.1f,
                radius * 30.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, Math::Vec3(0.0f, 0.0f, 0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        memset(&frame, 0, sizeof(frame));
        frame.viewProjection = projection * view;
        frame.inverseViewProjection = frame.viewProjection.Inverse();
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = kIblLuminance * exposure;
        frame.exposure[1] = blur;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));

        driver->beginRenderPass(skyPass);
        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);
        driver->endRenderPass();

        zenapp::renderColorPyramid(driver, pyramid);

        driver->beginRenderPass(scenePass);
        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        zenapp::bindGltfGeometry(driver, gpu);
        driver->bindPipeline(refractionPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindUniformBuffer(2, objectBuffer, 0, sizeof(ObjectUniforms));
        driver->bindUniformBuffer(7, refractionBuffer, 0, sizeof(RefractionUniforms));
        zenapp::bindIbl(driver, ibl);
        zenapp::bindColorPyramid(driver, pyramid, 2);
        for (size_t p = 0; p < model.primitives.size(); ++p)
            zenapp::drawGltfPrimitive(driver, gpu, model.primitives[p]);
        driver->endRenderPass();

        zenapp::renderPostProcess(driver, post, sceneColor, windowPass);
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(refractionPipeline);
    driver->destroy(refractionBuffer);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    driver->destroy(sceneDepth);
    driver->destroy(sceneColor);
    zenapp::destroyPostProcess(driver, &post);
    zenapp::destroyColorPyramid(driver, &pyramid);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
