#include "common/Equirect.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/SunShadow.h"
#include "common/SunShadowFit.h"
#include "common/PostProcess.h"
#include "common/Projection.h"
#include "common/Ssao.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "drone.frag.h"
#include "drone.vert.h"
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

const float kSunLux = 100000.0f;
const float kIblLuminance = 30000.0f;
const std::uint32_t kRenderWidth = 1280;
const std::uint32_t kRenderHeight = 720;
const float kAperture = 16.0f;
const float kShutter = 1.0f / 125.0f;
const float kSensitivity = 100.0f;

float exposureFactor()
{
    const float ev100 = log2f(kAperture * kAperture / kShutter * 100.0f / kSensitivity);
    return 1.0f / (1.2f * powf(2.0f, ev100));
}

void setTargetFormats(prisma::PipelineDesc* desc, bool depth)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = prisma::TextureFormat::RGBA16F;
    desc->targets.depth = depth ? prisma::TextureFormat::Depth32F : prisma::TextureFormat::None;
}

float numberArgument(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

Math::Mat4 nodeMatrix(const zenapp::GltfNode& node)
{
    Math::Mat4 m;
    memcpy(m.Data(), node.world, 16 * sizeof(float));
    return m;
}

void modelBounds(const zenapp::GltfModel& model, Math::Vec3* center, float* radius, Math::Vec3* lowOut,
        Math::Vec3* highOut)
{
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (size_t n = 0; n < model.nodes.size(); ++n)
    {
        if (model.nodes[n].mesh < 0) continue;
        const Math::Mat4 matrix = nodeMatrix(model.nodes[n]);
        const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
        for (unsigned p = 0; p < mesh.primitiveCount; ++p)
        {
            const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
            for (int corner = 0; corner < 8; ++corner)
            {
                const Math::Vec4 local((corner & 1) ? primitive.boundsMax[0] : primitive.boundsMin[0],
                        (corner & 2) ? primitive.boundsMax[1] : primitive.boundsMin[1],
                        (corner & 4) ? primitive.boundsMax[2] : primitive.boundsMin[2], 1.0f);
                const Math::Vec4 world = matrix * local;
                low = Math::Vec3(fminf(low.x, world.x), fminf(low.y, world.y), fminf(low.z, world.z));
                high = Math::Vec3(fmaxf(high.x, world.x), fmaxf(high.y, world.y),
                        fmaxf(high.z, world.z));
            }
        }
    }
    *lowOut = low;
    *highOut = high;
    *center = (low + high) * 0.5f;
    *radius = (high - low).Length() * 0.5f;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const float blur = numberArgument(argc, argv, "blur", 2.5f);
    const float yaw = numberArgument(argc, argv, "yaw", 0.0f);
    const float turn = numberArgument(argc, argv, "turn", 0.0f);
    const float ev = numberArgument(argc, argv, "ev", 0.0f);
    const float cameraHeight = numberArgument(argc, argv, "height", 0.08f);
    const float bloomStrength = numberArgument(argc, argv, "bloom", 0.10f);
    const float fxaaEnabled = numberArgument(argc, argv, "fxaa", 1.0f);
    const float ssaoEnabled = numberArgument(argc, argv, "ssao", 1.0f);
    const float ssaoDebug = zenapp::hasArgument(argc, argv, "aodebug") ? 1.0f : 0.0f;
    const unsigned shadowSize = static_cast<unsigned>(numberArgument(argc, argv, "shadowsize", 2048.0f));
    const float normalBias = numberArgument(argc, argv, "normalbias", 1.0f);

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "models/BusterDrone/scene.gltf");
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath), "%s/../environments/venetian_crossroads_2k.hdr",
                PRISMA_MODELS_DIR);

    zenapp::GltfModel model;
    if (!zenapp::loadGltf(modelPath, &model))
    {
        log_error("drone: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("drone: cannot read %s", environmentPath);
        return 1;
    }
    Math::Vec3 center;
    float radius;
    Math::Vec3 boundsLow;
    Math::Vec3 boundsHigh;
    modelBounds(model, &center, &radius, &boundsLow, &boundsHigh);
    const Math::Vec3 extent = boundsHigh - boundsLow;
    const float maxExtent = fmaxf(extent.x, fmaxf(extent.y, extent.z));
    const float unitScale = 2.0f / maxExtent;
    boundsLow = (boundsLow - center) * unitScale;
    boundsHigh = (boundsHigh - center) * unitScale;
    const Math::Vec3 modelCenter = center;
    center = Math::Vec3(0.0f, 0.0f, 0.0f);
    radius *= unitScale;

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 34 drone", driverType);
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
        log_error("drone: this GPU cannot render to float textures");
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
    log_info("drone: %u vertices, %u triangles, %u textures loaded, %u failed",
            static_cast<unsigned>(model.vertices.size()),
            static_cast<unsigned>(model.indices.size() / 3), gpu.texturesLoaded, gpu.texturesFailed);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned objectStride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
    ct::Vector<unsigned char> objectBytes;
    objectBytes.resize(static_cast<size_t>(objectStride) * (model.nodes.size() + 1));
    memset(objectBytes.data(), 0, objectBytes.size());
    for (size_t n = 0; n < model.nodes.size(); ++n)
    {
        ObjectUniforms object;
        object.model = Math::Mat4::Scale(Math::Vec3(unitScale, unitScale, unitScale)) *
                       Math::Mat4::RotationY(turn) * Math::Mat4::Translation(-modelCenter) *
                       nodeMatrix(model.nodes[n]);
        object.normalMatrix = object.model.Inverse().Transposed();
        memcpy(objectBytes.data() + n * objectStride, &object, sizeof(object));
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);
    prisma::BufferDesc objectDesc;
    objectDesc.usage = prisma::BufferUsage::Uniform;
    objectDesc.size = static_cast<std::uint32_t>(objectBytes.size());
    objectDesc.data = objectBytes.data();
    objectDesc.debugName = "object uniforms";
    const prisma::BufferHandle objectBuffer = driver->createBuffer(objectDesc);

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

    zenapp::PostProcess post;
    const bool postReady = zenapp::createPostProcess(driver, kRenderWidth, kRenderHeight,
            bloomStrength, fxaaEnabled > 0.5f, &post);

    const float aoFar = radius * 20.0f;
    const Math::Mat4 aoProjection = zenapp::perspectiveZeroToOne(0.6f,
            static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight), radius * 0.1f, aoFar);
    const Math::Mat4 aoInverseProjection = aoProjection.Inverse();
    zenapp::SsaoDesc ssaoDesc;
    ssaoDesc.width = kRenderWidth;
    ssaoDesc.height = kRenderHeight;
    ssaoDesc.projection = aoProjection.Data();
    ssaoDesc.inverseProjection = aoInverseProjection.Data();
    ssaoDesc.farPlane = aoFar;
    ssaoDesc.enabled = ssaoEnabled > 0.5f;
    ssaoDesc.debug = ssaoDebug > 0.5f;
    zenapp::Ssao ssao;
    const bool ssaoReady = zenapp::createSsao(driver, ssaoDesc, &ssao);

    zenapp::SunShadowParams sunParams;
    zenapp::fitSunShadow(boundsLow, boundsHigh, Math::Vec3(0.0f, 1.0f, 0.0f), shadowSize, normalBias,
            &sunParams);
    zenapp::SunShadow sunShadow;
    const bool sunShadowReady = zenapp::createSunShadow(driver, shadowSize, sunParams, &sunShadow);

    const prisma::ShaderHandle droneVertex = zenapp::createShader(driver, drone_vert);
    const prisma::ShaderHandle droneFragment = zenapp::createShader(driver, drone_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc opaqueDesc;
    opaqueDesc.vertexShader = droneVertex;
    opaqueDesc.fragmentShader = droneFragment;
    opaqueDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
    opaqueDesc.vertexBufferCount = 1;
    opaqueDesc.attributeCount = 4;
    opaqueDesc.attributes[0].location = 0;
    opaqueDesc.attributes[0].format = prisma::VertexFormat::Float3;
    opaqueDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
    opaqueDesc.attributes[1].location = 1;
    opaqueDesc.attributes[1].format = prisma::VertexFormat::Float3;
    opaqueDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, normal);
    opaqueDesc.attributes[2].location = 2;
    opaqueDesc.attributes[2].format = prisma::VertexFormat::Float4;
    opaqueDesc.attributes[2].offset = offsetof(zenapp::GltfVertex, tangent);
    opaqueDesc.attributes[3].location = 3;
    opaqueDesc.attributes[3].format = prisma::VertexFormat::Float2;
    opaqueDesc.attributes[3].offset = offsetof(zenapp::GltfVertex, uv);
    opaqueDesc.depthTest = true;
    opaqueDesc.cullMode = prisma::CullMode::Back;
    setTargetFormats(&opaqueDesc, true);
    opaqueDesc.debugName = "drone opaque";
    const prisma::PipelineHandle opaquePipeline = driver->createPipeline(opaqueDesc);

    prisma::PipelineDesc doubleDesc = opaqueDesc;
    doubleDesc.cullMode = prisma::CullMode::None;
    doubleDesc.debugName = "drone double sided";
    const prisma::PipelineHandle doublePipeline = driver->createPipeline(doubleDesc);

    prisma::PipelineDesc blendDesc = doubleDesc;
    blendDesc.depthWrite = false;
    blendDesc.blend = true;
    blendDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    blendDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.srcAlpha = prisma::BlendFactor::One;
    blendDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.debugName = "drone blend";
    const prisma::PipelineHandle blendPipeline = driver->createPipeline(blendDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    setTargetFormats(&skyDesc, true);
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(droneVertex);
    driver->destroy(droneFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool ready = iblReady && gpuReady && frameBuffer.valid() && objectBuffer.valid() &&
                       opaquePipeline.valid() && doublePipeline.valid() && blendPipeline.valid() &&
                       skyPipeline.valid() && sceneColor.valid() && sceneDepth.valid() &&
                       postReady && sunShadowReady && ssaoReady;
    if (!ready) log_error("drone: resource creation failed");

    prisma::RenderPassDesc scenePass;
    scenePass.colors[0].texture = sceneColor;
    scenePass.colorCount = 1;
    scenePass.depth.texture = sceneDepth;
    scenePass.depthStore = prisma::StoreOp::Discard;
    scenePass.clearColor[0] = scenePass.clearColor[1] = scenePass.clearColor[2] = 0.0f;
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
        const float angle = yaw + (still ? 0.0f : static_cast<float>(time_seconds()) * 0.3f);

        const float distance = radius * 2.2f;
        const Math::Vec3 eye(center.x + distance * sinf(angle), center.y + radius * cameraHeight,
                center.z - distance * cosf(angle));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.6f, aspect, radius * 0.1f,
                radius * 20.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, center, Math::Vec3(0.0f, 1.0f, 0.0f));

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
        frame.sunDirection[1] = 1.0f;
        frame.sunColorIntensity[0] = frame.sunColorIntensity[1] = frame.sunColorIntensity[2] = 1.0f;
        frame.sunColorIntensity[3] = kSunLux * exposure;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        zenapp::renderSunShadow(driver, &sunShadow, [&](prisma::PipelineHandle) {
            zenapp::bindGltfGeometry(driver, gpu);
            for (size_t n = 0; n < model.nodes.size(); ++n)
            {
                if (model.nodes[n].mesh < 0) continue;
                const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
                driver->bindUniformBuffer(2, objectBuffer, static_cast<std::uint32_t>(n * objectStride),
                        sizeof(ObjectUniforms));
                for (unsigned p = 0; p < mesh.primitiveCount; ++p)
                {
                    const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
                    if (primitive.material >= 0 &&
                            model.materials[static_cast<size_t>(primitive.material)].alpha ==
                                    zenapp::GltfMaterial::Alpha::Blend)
                        continue;
                    zenapp::drawGltfPrimitive(driver, gpu, primitive);
                }
            }
        });

        zenapp::renderSsao(driver, ssao, [&](prisma::PipelineHandle single, prisma::PipelineHandle doubled) {
            zenapp::bindGltfGeometry(driver, gpu);
            for (size_t n = 0; n < model.nodes.size(); ++n)
            {
                if (model.nodes[n].mesh < 0) continue;
                const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
                for (unsigned p = 0; p < mesh.primitiveCount; ++p)
                {
                    const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
                    if (primitive.material < 0) continue;
                    const zenapp::GltfMaterial& material =
                            model.materials[static_cast<size_t>(primitive.material)];
                    if (material.alpha == zenapp::GltfMaterial::Alpha::Blend) continue;
                    driver->bindPipeline(material.doubleSided ? doubled : single);
                    driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
                    driver->bindUniformBuffer(2, objectBuffer,
                            static_cast<std::uint32_t>(n * objectStride), sizeof(ObjectUniforms));
                    zenapp::drawGltfPrimitive(driver, gpu, primitive);
                }
            }
        });

        driver->beginRenderPass(scenePass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        zenapp::bindGltfGeometry(driver, gpu);
        for (int blended = 0; blended < 2; ++blended)
        {
            for (size_t n = 0; n < model.nodes.size(); ++n)
            {
                if (model.nodes[n].mesh < 0) continue;
                const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
                for (unsigned p = 0; p < mesh.primitiveCount; ++p)
                {
                    const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
                    if (primitive.material < 0) continue;
                    const zenapp::GltfMaterial& material =
                            model.materials[static_cast<size_t>(primitive.material)];
                    const bool isBlend = material.alpha == zenapp::GltfMaterial::Alpha::Blend;
                    if (isBlend != (blended == 1)) continue;
                    driver->bindPipeline(isBlend ? blendPipeline
                                                 : (material.doubleSided ? doublePipeline
                                                                         : opaquePipeline));
                    driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
                    zenapp::bindIbl(driver, ibl);
                    zenapp::bindSunShadow(driver, sunShadow);
                    zenapp::bindSsao(driver, ssao);
                    driver->bindUniformBuffer(2, objectBuffer, static_cast<std::uint32_t>(n * objectStride),
                            sizeof(ObjectUniforms));
                    zenapp::bindGltfMaterial(driver, gpu, model, primitive.material);
                    zenapp::drawGltfPrimitive(driver, gpu, primitive);
                }
            }
        }
        driver->endRenderPass();

        zenapp::renderPostProcess(driver, post, sceneColor, windowPass);
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(blendPipeline);
    driver->destroy(doublePipeline);
    driver->destroy(opaquePipeline);
    zenapp::destroySsao(driver, &ssao);
    zenapp::destroySunShadow(driver, &sunShadow);
    zenapp::destroyPostProcess(driver, &post);
    driver->destroy(sceneDepth);
    driver->destroy(sceneColor);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
