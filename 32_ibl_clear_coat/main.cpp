#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/Equirect.h"
#include "common/ObjModel.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "coat.frag.h"
#include "coat.vert.h"
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
    float baseColor[4];
    float material[4];
    Math::Mat4 model;
};

const float kSunLux = 110000.0f;
const float kIblLuminance = 40000.0f;
const float kAperture = 16.0f;
const float kShutter = 1.0f / 125.0f;
const float kSensitivity = 100.0f;
const float kRadius = 4.5f;
const float kHeight = 1.5f;
const float kLift = -1.2f;

float exposureFactor()
{
    const float ev100 = log2f(kAperture * kAperture / kShutter * 100.0f / kSensitivity);
    return 1.0f / (1.2f * powf(2.0f, ev100));
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float numberArgument(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const float blur = numberArgument(argc, argv, "blur", 2.5f);

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "%s/shader_ball/shader_ball.obj", PRISMA_MODELS_DIR);
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath),
                "%s/../environments/flower_road_no_sun_2k.hdr", PRISMA_MODELS_DIR);

    zenapp::GltfModel model;
    if (!zenapp::loadObj(modelPath, &model))
    {
        log_error("ibl clear coat: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("ibl clear coat: cannot read %s", environmentPath);
        return 1;
    }

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 32 ibl clear coat", driverType);
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
        log_error("ibl clear coat: this GPU cannot render to float textures");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    const float exposure = exposureFactor();
    zenapp::Ibl ibl;
    const bool iblReady = zenapp::createIbl(driver, faces, &ibl, kIblLuminance * exposure);
    if (!iblReady) log_error("ibl clear coat: cannot build the image based lighting resources");

    zenapp::GltfGpu gpu;
    zenapp::GltfGpuOptions gpuOptions;
    const bool gpuReady = zenapp::createGltfGpu(driver, model, gpuOptions, &gpu);

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle coatVertex = zenapp::createShader(driver, coat_vert);
    const prisma::ShaderHandle coatFragment = zenapp::createShader(driver, coat_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc coatDesc;
    coatDesc.vertexShader = coatVertex;
    coatDesc.fragmentShader = coatFragment;
    coatDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
    coatDesc.vertexBufferCount = 1;
    coatDesc.attributeCount = 2;
    coatDesc.attributes[0].location = 0;
    coatDesc.attributes[0].format = prisma::VertexFormat::Float3;
    coatDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
    coatDesc.attributes[1].location = 1;
    coatDesc.attributes[1].format = prisma::VertexFormat::Float3;
    coatDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, normal);
    coatDesc.depthTest = true;
    coatDesc.cullMode = prisma::CullMode::Back;
    coatDesc.debugName = "clear coat pipeline";
    const prisma::PipelineHandle coatPipeline = driver->createPipeline(coatDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(coatVertex);
    driver->destroy(coatFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool ready = iblReady && gpuReady && frameBuffer.valid() && coatPipeline.valid() &&
                       skyPipeline.valid();
    if (!ready) log_error("ibl clear coat: resource creation failed");
    log_info("ibl clear coat: %u vertices, %u triangles, environment %u pixels", 
            static_cast<unsigned>(model.vertices.size()),
            static_cast<unsigned>(model.indices.size() / 3), faces.size);

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;

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
        const float angle = still ? 0.0f : static_cast<float>(time_seconds()) * (6.2831853f / 18.0f);

        const Math::Vec3 eye(cosf(angle) * kRadius, kHeight, sinf(angle) * kRadius);
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.7853982f, aspect, 0.1f, 20.0f);
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
        const float toLight[3] = { 0.753f, 1.0f, -0.890f };
        const float toLightLength = sqrtf(toLight[0] * toLight[0] + toLight[1] * toLight[1] +
                                          toLight[2] * toLight[2]);
        for (int i = 0; i < 3; ++i) frame.sunDirection[i] = toLight[i] / toLightLength;
        frame.sunColorIntensity[0] = frame.sunColorIntensity[1] = frame.sunColorIntensity[2] = 1.0f;
        frame.sunColorIntensity[3] = kSunLux * exposure;
        frame.baseColor[0] = srgbToLinear(0.71f);
        frame.material[0] = 1.0f;
        frame.material[1] = 0.65f;
        frame.material[2] = 1.0f;
        frame.material[3] = 0.0f;
        frame.model = Math::Mat4::Translation(Math::Vec3(0.0f, kLift, 0.0f));

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        driver->bindPipeline(coatPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        zenapp::bindIbl(driver, ibl);
        zenapp::bindGltfGeometry(driver, gpu);
        for (size_t i = 0; i < model.primitives.size(); ++i)
            zenapp::drawGltfPrimitive(driver, gpu, model.primitives[i]);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(coatPipeline);
    driver->destroy(frameBuffer);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
