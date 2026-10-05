#include "common/Equirect.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "helmet.frag.h"
#include "helmet.vert.h"
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

const float kSunLux = 110000.0f;
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

Math::Mat4 nodeMatrix(const zenapp::GltfNode& node)
{
    Math::Mat4 m;
    memcpy(m.Data(), node.world, 16 * sizeof(float));
    return m;
}

void modelBounds(const zenapp::GltfModel& model, Math::Vec3* center, float* radius)
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
    *center = (low + high) * 0.5f;
    *radius = (high - low).Length() * 0.5f;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const float blur = numberArgument(argc, argv, "blur", 3.0f);
    const float yaw = numberArgument(argc, argv, "yaw", 2.3f);
    const float turn = numberArgument(argc, argv, "turn", 1.5f);
    const float ev = numberArgument(argc, argv, "ev", 0.0f);

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "models/FlightHelmet/FlightHelmet.gltf");
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath), "%s/../environments/flower_road_no_sun_2k.hdr",
                PRISMA_MODELS_DIR);

    zenapp::GltfModel model;
    if (!zenapp::loadGltf(modelPath, &model))
    {
        log_error("flight helmet: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("flight helmet: cannot read %s", environmentPath);
        return 1;
    }
    Math::Vec3 center;
    float radius;
    modelBounds(model, &center, &radius);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 33 flight helmet", driverType);
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
        log_error("flight helmet: this GPU cannot render to float textures");
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
    log_info("flight helmet: %u vertices, %u triangles, %u textures loaded, %u failed",
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
        object.model = Math::Mat4::Translation(center) * Math::Mat4::RotationY(turn) *
                       Math::Mat4::Translation(-center) * nodeMatrix(model.nodes[n]);
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

    const prisma::ShaderHandle helmetVertex = zenapp::createShader(driver, helmet_vert);
    const prisma::ShaderHandle helmetFragment = zenapp::createShader(driver, helmet_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc opaqueDesc;
    opaqueDesc.vertexShader = helmetVertex;
    opaqueDesc.fragmentShader = helmetFragment;
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
    opaqueDesc.debugName = "helmet opaque";
    const prisma::PipelineHandle opaquePipeline = driver->createPipeline(opaqueDesc);

    prisma::PipelineDesc doubleDesc = opaqueDesc;
    doubleDesc.cullMode = prisma::CullMode::None;
    doubleDesc.debugName = "helmet double sided";
    const prisma::PipelineHandle doublePipeline = driver->createPipeline(doubleDesc);

    prisma::PipelineDesc blendDesc = doubleDesc;
    blendDesc.depthWrite = false;
    blendDesc.blend = true;
    blendDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    blendDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.srcAlpha = prisma::BlendFactor::One;
    blendDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.debugName = "helmet blend";
    const prisma::PipelineHandle blendPipeline = driver->createPipeline(blendDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(helmetVertex);
    driver->destroy(helmetFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool ready = iblReady && gpuReady && frameBuffer.valid() && objectBuffer.valid() &&
                       opaquePipeline.valid() && doublePipeline.valid() && blendPipeline.valid() &&
                       skyPipeline.valid();
    if (!ready) log_error("flight helmet: resource creation failed");

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
        const float angle = yaw + (still ? 0.0f : static_cast<float>(time_seconds()) * 0.3f);

        const float distance = radius * 3.3f;
        const Math::Vec3 eye(center.x + distance * sinf(angle), center.y + radius * 0.45f,
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
        const float sunAngle = angle - 0.9f;
        const float toLight[3] = { sinf(sunAngle), 1.0f, -cosf(sunAngle) };
        const float length = sqrtf(toLight[0] * toLight[0] + toLight[1] * toLight[1] +
                                   toLight[2] * toLight[2]);
        for (int i = 0; i < 3; ++i) frame.sunDirection[i] = toLight[i] / length;
        frame.sunColorIntensity[0] = frame.sunColorIntensity[1] = frame.sunColorIntensity[2] = 1.0f;
        frame.sunColorIntensity[3] = kSunLux * exposure;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);

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
                    driver->bindUniformBuffer(2, objectBuffer, static_cast<std::uint32_t>(n * objectStride),
                            sizeof(ObjectUniforms));
                    zenapp::bindGltfMaterial(driver, gpu, model, primitive.material);
                    zenapp::drawGltfPrimitive(driver, gpu, primitive);
                }
            }
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(blendPipeline);
    driver->destroy(doublePipeline);
    driver->destroy(opaquePipeline);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
