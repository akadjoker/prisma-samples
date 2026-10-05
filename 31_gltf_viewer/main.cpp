#include "common/FrameLog.h"
#include "common/StatsOverlay.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/LightShadows.h"
#include "common/Lights.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/sort.hpp>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "depth.frag.h"
#include "depth.vert.h"
#include "depth_opaque.frag.h"
#include "gltf.frag.h"
#include "gltf.vert.h"
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
};

struct ObjectUniforms
{
    Math::Mat4 model;
    Math::Mat4 normalMatrix;
};

struct Draw
{
    unsigned node;
    unsigned primitive;
    int material;
    float distance;
    bool blend;
    bool doubleSided;
    bool mask;
};

struct Candidate
{
    Draw draw;
    Math::Box box;
    Math::Vec3 center;
    float diameter;
};

Math::Mat4 nodeMatrix(const float* world)
{
    Math::Mat4 m;
    memcpy(m.Data(), world, 16 * sizeof(float));
    return m;
}

Math::Mat4 nodeMatrix(const zenapp::GltfNode& node)
{
    return nodeMatrix(node.world);
}

Math::Box modelBounds(const zenapp::GltfModel& model)
{
    Math::Box bounds(Math::Vec3(1e30f, 1e30f, 1e30f), Math::Vec3(-1e30f, -1e30f, -1e30f));
    bool any = false;
    for (const zenapp::GltfNode& node: model.nodes)
    {
        if (node.mesh < 0) continue;
        const Math::Mat4 matrix = nodeMatrix(node);
        const zenapp::GltfMesh& mesh = model.meshes[node.mesh];
        for (unsigned p = 0; p < mesh.primitiveCount; ++p)
        {
            const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
            const Math::Box box = Math::Box(Math::Vec3(primitive.boundsMin[0], primitive.boundsMin[1],
                                                    primitive.boundsMin[2]),
                    Math::Vec3(primitive.boundsMax[0], primitive.boundsMax[1], primitive.boundsMax[2]))
                                          .Transformed(matrix);
            bounds = any ? bounds.Union(box) : box;
            any = true;
        }
    }
    return bounds;
}

float lightRadius(const zenapp::GltfLight& light, float intensity)
{
    if (light.range > 0.0f) return light.range;
    const float radius = sqrtf(intensity / 0.05f);
    return radius < 1.0f ? 1.0f : (radius > 60.0f ? 60.0f : radius);
}

float numberArgument(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

void addModelLights(const zenapp::GltfModel& model, float scale, float sunScale,
        zenapp::LightSet* set)
{
    for (const zenapp::GltfLight& light: model.lights)
    {
        const float intensity = light.intensity * scale;
        if (light.type == zenapp::GltfLight::Type::Directional)
        {
            const float toLight[3] = { -light.direction[0], -light.direction[1], -light.direction[2] };
            zenapp::setSunLight(set, toLight, light.color, light.intensity * sunScale);
        }
        else if (light.type == zenapp::GltfLight::Type::Spot)
            zenapp::addSpotLight(set, light.position, light.direction, light.color, intensity,
                    lightRadius(light, intensity), light.innerCone, light.outerCone);
        else
            zenapp::addPointLight(set, light.position, light.color, intensity,
                    lightRadius(light, intensity));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "%s/DamagedHelmet/DamagedHelmet.glb",
                PRISMA_MODELS_DIR);
    const char* exposureArgument = zenapp::argumentValue(argc, argv, "exposure");
    const float exposure = exposureArgument ? static_cast<float>(atof(exposureArgument)) : 1.0f;
    const float lightScale = numberArgument(argc, argv, "lightscale", 1.0f);
    const float sunScale = numberArgument(argc, argv, "sunscale", 1.0f);
    const float iblScale = numberArgument(argc, argv, "ibl", 1.0f);
    const float skyScale = numberArgument(argc, argv, "sky", 1.0f);
    const char* skipArgument = zenapp::argumentValue(argc, argv, "skipmips");
    const unsigned skipMips = skipArgument ? static_cast<unsigned>(atoi(skipArgument)) : 0;
    const bool useFileCamera = zenapp::hasArgument(argc, argv, "camera");
    const bool usePrepass = zenapp::hasArgument(argc, argv, "prepass");
    const float minPixels = numberArgument(argc, argv, "minpixels", 12.0f);
    const bool skipMain = zenapp::hasArgument(argc, argv, "skipmain");
    const bool noLights = zenapp::hasArgument(argc, argv, "nolights");
    const bool noBlend = zenapp::hasArgument(argc, argv, "noblend");
    const bool noMask = zenapp::hasArgument(argc, argv, "nomask");
    const bool autoWalk = zenapp::hasArgument(argc, argv, "autowalk");
    const unsigned shadowLights =
            static_cast<unsigned>(numberArgument(argc, argv, "shadows", 32.0f));
    const unsigned shadowSize =
            static_cast<unsigned>(numberArgument(argc, argv, "shadowsize", 256.0f));
    const float shadowNear = numberArgument(argc, argv, "shadownear", 0.3f);
    const float shadowDistance = numberArgument(argc, argv, "shadowdistance", 30.0f);

    zenapp::GltfModel model;
    if (!zenapp::loadGltf(modelPath, &model))
    {
        log_error("gltf viewer: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    log_info("gltf viewer: %u vertices, %u triangles, %u nodes, %u materials, %u textures, %u lights",
            static_cast<unsigned>(model.vertices.size()), static_cast<unsigned>(model.indices.size() / 3),
            static_cast<unsigned>(model.nodes.size()), static_cast<unsigned>(model.materials.size()),
            static_cast<unsigned>(model.textures.size()), static_cast<unsigned>(model.lights.size()));

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 31 gltf viewer", driverType);
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
        log_error("gltf viewer: this GPU cannot render to float textures");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("Light Probes/uffizi_cross.dds", path, sizeof(path));
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEnvironmentFaces(path, &faces))
    {
        log_error("gltf viewer: cannot read %s", path);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 1;
    }
    zenapp::Ibl ibl;
    const bool iblReady = zenapp::createIbl(driver, faces, &ibl);

    zenapp::GltfGpuOptions gpuOptions;
    gpuOptions.skipMips = skipMips;
    gpuOptions.anisotropy = numberArgument(argc, argv, "aniso", 4.0f);
    zenapp::GltfGpu gpu;
    const bool gpuReady = zenapp::createGltfGpu(driver, model, gpuOptions, &gpu);
    log_info("gltf viewer: %u textures loaded, %u failed", gpu.texturesLoaded, gpu.texturesFailed);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned frameStride = (sizeof(FrameUniforms) + alignment - 1) / alignment * alignment;
    const unsigned objectStride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
    unsigned maxDraws = 0;
    for (const zenapp::GltfNode& node: model.nodes)
        if (node.mesh >= 0) maxDraws += model.meshes[node.mesh].primitiveCount;
    const unsigned identityNode = static_cast<unsigned>(model.nodes.size());
    ct::Vector<ObjectUniforms> nodeUniforms;
    nodeUniforms.resize(model.nodes.size() + 1);
    ct::Vector<unsigned char> objectBytes;
    objectBytes.resize(static_cast<size_t>(objectStride) * nodeUniforms.size());
    memset(objectBytes.data(), 0, objectBytes.size());
    for (unsigned n = 0; n < nodeUniforms.size(); ++n)
    {
        nodeUniforms[n].model = n < identityNode ? nodeMatrix(model.nodes[n]) : Math::Mat4::Identity();
        nodeUniforms[n].normalMatrix = nodeUniforms[n].model.Inverse().Transposed();
        memcpy(objectBytes.data() + static_cast<size_t>(n) * objectStride, &nodeUniforms[n],
                sizeof(ObjectUniforms));
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = frameStride;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);
    prisma::BufferDesc objectDesc;
    objectDesc.usage = prisma::BufferUsage::Uniform;
    objectDesc.size = static_cast<std::uint32_t>(objectBytes.size());
    objectDesc.data = objectBytes.data();
    objectDesc.debugName = "object uniforms";
    const prisma::BufferHandle objectBuffer = driver->createBuffer(objectDesc);
    static zenapp::ClusteredBuffers clustered;
    bufferDesc.size = sizeof(clustered);
    bufferDesc.debugName = "clustered lights";
    const prisma::BufferHandle clusteredBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle gltfVertex = zenapp::createShader(driver, gltf_vert);
    const prisma::ShaderHandle gltfFragment = zenapp::createShader(driver, gltf_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc opaqueDesc;
    opaqueDesc.vertexShader = gltfVertex;
    opaqueDesc.fragmentShader = gltfFragment;
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
    opaqueDesc.depthWrite = !usePrepass;
    opaqueDesc.depthCompare = usePrepass ? prisma::CompareOp::Equal : prisma::CompareOp::Less;
    opaqueDesc.cullMode = prisma::CullMode::Back;
    opaqueDesc.debugName = "gltf opaque";
    const prisma::PipelineHandle opaquePipeline = driver->createPipeline(opaqueDesc);

    prisma::PipelineDesc doubleDesc = opaqueDesc;
    doubleDesc.cullMode = prisma::CullMode::None;
    doubleDesc.debugName = "gltf double sided";
    const prisma::PipelineHandle doublePipeline = driver->createPipeline(doubleDesc);

    prisma::PipelineDesc blendDesc = doubleDesc;
    blendDesc.depthWrite = false;
    blendDesc.depthCompare = prisma::CompareOp::Less;
    blendDesc.blend = true;
    blendDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    blendDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.srcAlpha = prisma::BlendFactor::One;
    blendDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.debugName = "gltf blend";
    const prisma::PipelineHandle blendPipeline = driver->createPipeline(blendDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    const prisma::ShaderHandle depthVertex = zenapp::createShader(driver, depth_vert);
    const prisma::ShaderHandle depthMaskFragment = zenapp::createShader(driver, depth_frag);
    const prisma::ShaderHandle depthOpaqueFragment = zenapp::createShader(driver, depth_opaque_frag);
    prisma::PipelineHandle depthPipelines[4];
    prisma::PipelineHandle shadowPipelines[4];
    for (int variant = 0; variant < 4; ++variant)
    {
        prisma::PipelineDesc depthDesc;
        depthDesc.vertexShader = depthVertex;
        depthDesc.fragmentShader = (variant & 1) ? depthMaskFragment : depthOpaqueFragment;
        depthDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
        depthDesc.vertexBufferCount = 1;
        depthDesc.attributeCount = 2;
        depthDesc.attributes[0].location = 0;
        depthDesc.attributes[0].format = prisma::VertexFormat::Float3;
        depthDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
        depthDesc.attributes[1].location = 3;
        depthDesc.attributes[1].format = prisma::VertexFormat::Float2;
        depthDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, uv);
        depthDesc.depthTest = true;
        depthDesc.cullMode = (variant & 2) ? prisma::CullMode::None : prisma::CullMode::Back;
        depthDesc.colorMask = 0;
        depthDesc.debugName = "gltf depth";
        depthPipelines[variant] = driver->createPipeline(depthDesc);

        depthDesc.targets.window = false;
        depthDesc.targets.colorCount = 0;
        depthDesc.targets.depth = prisma::TextureFormat::Depth32F;
        depthDesc.depthBiasConstant = 2.0f;
        depthDesc.depthBiasSlope = 2.0f;
        depthDesc.debugName = "gltf shadow";
        shadowPipelines[variant] = driver->createPipeline(depthDesc);
    }
    driver->destroy(depthVertex);
    driver->destroy(depthMaskFragment);
    driver->destroy(depthOpaqueFragment);
    driver->destroy(gltfVertex);
    driver->destroy(gltfFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool baseReady = iblReady && gpuReady && frameBuffer.valid() && objectBuffer.valid() &&
                           clusteredBuffer.valid() && opaquePipeline.valid() &&
                           doublePipeline.valid() && depthPipelines[0].valid() &&
                           depthPipelines[1].valid() && depthPipelines[2].valid() &&
                           depthPipelines[3].valid() && shadowPipelines[0].valid() &&
                           shadowPipelines[1].valid() && shadowPipelines[2].valid() &&
                           shadowPipelines[3].valid() && blendPipeline.valid() &&
                           skyPipeline.valid();

    const Math::Box bounds = modelBounds(model);
    const Math::Vec3 center = bounds.Center();
    const float radius = bounds.Extents().Length();
    zenapp::Froxelizer froxelizer;
    const bool fileCamera = useFileCamera && model.camera.valid;
    froxelizer.setDepthRange(fileCamera ? 3.0f : radius * 0.3f, fileCamera ? 150.0f : radius * 8.0f);

    zenapp::LightSet lights;
    zenapp::clearLights(&lights);
    if (model.lights.size() == 0 && !zenapp::hasArgument(argc, argv, "nosun"))
    {
        const float toLight[3] = { 0.4f, 0.8f, 0.5f };
        const float color[3] = { 1.0f, 0.96f, 0.9f };
        zenapp::setSunLight(&lights, toLight, color, 2.0f);
    }
    if (!noLights) addModelLights(model, lightScale, sunScale, &lights);

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0.0f;

    ct::Vector<Draw> draws;
    draws.reserve(maxDraws);
    ct::Vector<unsigned> depthOrder;
    depthOrder.reserve(maxDraws);

    ct::Vector<Candidate> candidates;
    candidates.reserve(maxDraws);
    for (unsigned n = 0; n < model.nodes.size(); ++n)
    {
        const zenapp::GltfNode& node = model.nodes[n];
        if (node.mesh < 0) continue;
        const zenapp::GltfMesh& mesh = model.meshes[node.mesh];
        for (unsigned p = 0; p < mesh.primitiveCount; ++p)
        {
            const unsigned index = mesh.firstPrimitive + p;
            const zenapp::GltfPrimitive& primitive = model.primitives[index];
            const zenapp::GltfMaterial* material =
                    primitive.material >= 0 ? &model.materials[primitive.material] : nullptr;
            Candidate candidate;
            candidate.draw.node = n;
            candidate.draw.primitive = index;
            candidate.draw.material = primitive.material;
            candidate.draw.distance = 0.0f;
            candidate.draw.blend =
                    material && material->alpha == zenapp::GltfMaterial::Alpha::Blend;
            candidate.draw.mask = material && material->alpha == zenapp::GltfMaterial::Alpha::Mask;
            candidate.draw.doubleSided = material && material->doubleSided;
            if ((noBlend && candidate.draw.blend) || (noMask && candidate.draw.mask)) continue;
            candidate.box = Math::Box(Math::Vec3(primitive.boundsMin[0], primitive.boundsMin[1],
                                              primitive.boundsMin[2]),
                    Math::Vec3(primitive.boundsMax[0], primitive.boundsMax[1],
                            primitive.boundsMax[2])).Transformed(nodeUniforms[n].model);
            candidate.center = candidate.box.Center();
            candidate.diameter = candidate.box.Extents().Length() * 2.0f;
            candidates.push_back(candidate);
        }
    }

    Math::Vec3 flyEye(0.0f, 0.0f, 0.0f);
    float flyYaw = 0.0f;
    float flyPitch = 0.0f;
    if (fileCamera)
    {
        flyEye = Math::Vec3(model.camera.world[12], model.camera.world[13], model.camera.world[14]);
        const Math::Vec3 forward(-model.camera.world[8], -model.camera.world[9], -model.camera.world[10]);
        flyYaw = atan2f(forward.x, -forward.z);
        flyPitch = asinf(forward.y);
    }
    zenapp::ShadowSlots shadowSlots;
    zenapp::clearShadowSlots(&shadowSlots, shadowLights);
    const unsigned shadowCount = shadowSlots.count * zenapp::ShadowSlots::kMapsPerSlot;
    static zenapp::ShadowUniforms shadowUniforms;
    memset(&shadowUniforms, 0, sizeof(shadowUniforms));
    shadowUniforms.params[0] = shadowSize > 0 ? 1.0f / static_cast<float>(shadowSize) : 0.0f;
    shadowUniforms.params[1] = 1.5f;
    shadowUniforms.params[2] = static_cast<float>(shadowCount);

    prisma::TextureDesc shadowMapDesc;
    shadowMapDesc.type = prisma::TextureType::Texture2DArray;
    shadowMapDesc.format = prisma::TextureFormat::Depth32F;
    shadowMapDesc.width = shadowCount > 0 ? shadowSize : 1;
    shadowMapDesc.height = shadowCount > 0 ? shadowSize : 1;
    shadowMapDesc.depth = shadowCount > 0 ? shadowCount : 1;
    shadowMapDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    shadowMapDesc.debugName = "light shadow maps";
    const prisma::TextureHandle shadowMap = driver->createTexture(shadowMapDesc);

    prisma::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.mipFilter = prisma::MipFilter::None;
    shadowSamplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    shadowSamplerDesc.compare = true;
    shadowSamplerDesc.compareOp = prisma::CompareOp::LessEqual;
    shadowSamplerDesc.debugName = "light shadow sampler";
    const prisma::SamplerHandle shadowSampler = driver->createSampler(shadowSamplerDesc);

    prisma::BufferDesc shadowUniformDesc;
    shadowUniformDesc.usage = prisma::BufferUsage::Uniform;
    shadowUniformDesc.size = sizeof(shadowUniforms);
    shadowUniformDesc.data = &shadowUniforms;
    shadowUniformDesc.update = prisma::BufferUpdate::Dynamic;
    shadowUniformDesc.debugName = "light shadow uniforms";
    const prisma::BufferHandle shadowUniformBuffer = driver->createBuffer(shadowUniformDesc);

    ct::Vector<unsigned char> shadowFrameBytes;
    shadowFrameBytes.resize(static_cast<size_t>(frameStride) * (shadowCount > 0 ? shadowCount : 1));
    memset(shadowFrameBytes.data(), 0, shadowFrameBytes.size());
    prisma::BufferDesc shadowFrameDesc;
    shadowFrameDesc.usage = prisma::BufferUsage::Uniform;
    shadowFrameDesc.size = static_cast<std::uint32_t>(shadowFrameBytes.size());
    shadowFrameDesc.data = shadowFrameBytes.data();
    shadowFrameDesc.update = prisma::BufferUpdate::Dynamic;
    shadowFrameDesc.debugName = "light shadow frames";
    const prisma::BufferHandle shadowFrameBuffer = driver->createBuffer(shadowFrameDesc);

    const bool ready = baseReady && shadowMap.valid() && shadowSampler.valid() &&
                       shadowUniformBuffer.valid() && shadowFrameBuffer.valid();
    if (!ready) log_error("gltf viewer: resource creation failed");
    log_info("gltf viewer: %u shadow maps of %u pixels for up to %u lights", shadowCount,
            shadowSize, shadowSlots.count);

    if (ready && !zenapp::hasArgument(argc, argv, "nowarmup"))
    {
        ct::Vector<int> firstPrimitive;
        firstPrimitive.resize(model.materials.size());
        for (size_t m = 0; m < firstPrimitive.size(); ++m) firstPrimitive[m] = -1;
        for (size_t p = 0; p < model.primitives.size(); ++p)
        {
            const int m = model.primitives[p].material;
            if (m >= 0 && firstPrimitive[m] < 0) firstPrimitive[m] = static_cast<int>(p);
        }

        FrameUniforms warmFrame;
        memset(&warmFrame, 0, sizeof(warmFrame));
        warmFrame.camera[3] = 1.0f;
        memset(&clustered, 0, sizeof(clustered));
        prisma::Rect pixel;
        pixel.width = 1;
        pixel.height = 1;
        const size_t perFrame = 16;
        for (size_t first = 0; first < model.materials.size(); first += perFrame)
        {
            driver->beginFrame();
            driver->updateBuffer(frameBuffer, 0, &warmFrame, sizeof(warmFrame));
            driver->updateBuffer(clusteredBuffer, 0, &clustered, sizeof(clustered));
            driver->beginRenderPass(pass);
            driver->setScissor(pixel);
            zenapp::bindIbl(driver, ibl);
            driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
            driver->bindUniformBuffer(2, objectBuffer, identityNode * objectStride,
                    sizeof(ObjectUniforms));
            driver->bindUniformBuffer(3, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, cluster),
                    sizeof(zenapp::ClusterUniforms));
            driver->bindUniformBuffer(4, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, lights),
                    sizeof(clustered.lights));
            driver->bindUniformBuffer(5, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, froxels),
                    sizeof(clustered.froxels));
            driver->bindUniformBuffer(6, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, records),
                    sizeof(clustered.records));
            driver->bindUniformBuffer(8, shadowUniformBuffer, 0, sizeof(shadowUniforms));
            driver->bindTexture(7, shadowMap, shadowSampler);
            zenapp::bindGltfGeometry(driver, gpu);
            for (size_t m = first; m < first + perFrame && m < model.materials.size(); ++m)
            {
                if (firstPrimitive[m] < 0) continue;
                const zenapp::GltfMaterial& material = model.materials[m];
                const bool blend = material.alpha == zenapp::GltfMaterial::Alpha::Blend;
                driver->bindPipeline(blend ? blendPipeline
                                     : material.doubleSided ? doublePipeline
                                                            : opaquePipeline);
                zenapp::bindGltfMaterial(driver, gpu, model, static_cast<int>(m));
                zenapp::drawGltfPrimitive(driver, gpu, model.primitives[firstPrimitive[m]]);
            }
            driver->endRenderPass();
            driver->endFrame();
            driver->present();
        }
        froxelizer.setDepthRange(fileCamera ? 3.0f : radius * 0.3f, fileCamera ? 150.0f : radius * 8.0f);
    }

    Math::Vec3 previousEye = flyEye;
    float clusteredView[16] = {};
    float clusteredProjection[16] = {};
    int clusteredWidth = 0;
    int clusteredHeight = 0;
    bool clusteredValid = false;
    bool lightsDirty = false;
    double lastTime = time_seconds();
    if (zenapp::hasArgument(argc, argv, "novsync")) window_set_vsync(window, false);
    static const char* const phaseNames[4] = { "cull", "uniforms", "record", "present" };
    zenapp::FrameStats stats(4);
    zenapp::FrameLog frameLog;
    frameLog.reserve(20000);
    float lastGpu = 0.0f;
    float lastStep = 0.0f;
    unsigned lastKeys = 0;
    unsigned visibleTriangles = 0;
    const char* reportPath = zenapp::argumentValue(argc, argv, "report");
    if (!reportPath) reportPath = "gltf_viewer_report.txt";
    prisma::QueryHandle gpuQuery;
    if (driver->caps().timerQueries) gpuQuery = driver->createQuery(prisma::QueryType::Time);
    zenapp::TextOverlay overlay;
    const bool overlayReady = overlay.create(driver);
    bool showStats = !zenapp::hasArgument(argc, argv, "nostats");
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        stats.begin();
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_F1)) showStats = !showStats;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = still ? 0.0f : static_cast<float>(time_seconds());

        float fov = 0.7f;
        float nearPlane = radius * 0.02f;
        float farPlane = radius * 20.0f;
        Math::Vec3 eye;
        Math::Mat4 view;
        if (fileCamera)
        {
            const double now = time_seconds();
            const float delta = static_cast<float>(now - lastTime);
            lastTime = now;
            int mouseDx = 0;
            int mouseDy = 0;
            mouse_delta(window, &mouseDx, &mouseDy);
            const float turn = 1.6f * delta;
            if (mouse_button_down(window, MOUSE_RIGHT))
            {
                flyYaw += 0.004f * static_cast<float>(mouseDx);
                flyPitch -= 0.004f * static_cast<float>(mouseDy);
            }
            if (key_down(window, KEY_LEFT)) flyYaw -= turn;
            if (key_down(window, KEY_RIGHT)) flyYaw += turn;
            if (key_down(window, KEY_UP)) flyPitch += turn;
            if (key_down(window, KEY_DOWN)) flyPitch -= turn;
            flyPitch = flyPitch > 1.5f ? 1.5f : (flyPitch < -1.5f ? -1.5f : flyPitch);
            const Math::Vec3 forward(sinf(flyYaw) * cosf(flyPitch), sinf(flyPitch),
                    -cosf(flyYaw) * cosf(flyPitch));
            const Math::Vec3 right(cosf(flyYaw), 0.0f, sinf(flyYaw));
            const float speed = (key_down(window, KEY_LEFT_SHIFT) ? 12.0f : 3.0f) * delta;
            if (key_down(window, KEY_W)) flyEye = flyEye + forward * speed;
            if (key_down(window, KEY_S)) flyEye = flyEye - forward * speed;
            if (key_down(window, KEY_D)) flyEye = flyEye + right * speed;
            if (key_down(window, KEY_A)) flyEye = flyEye - right * speed;
            if (key_down(window, KEY_E)) flyEye.y += speed;
            if (key_down(window, KEY_Q)) flyEye.y -= speed;
            if (autoWalk)
            {
                flyYaw += 0.5f * delta;
                flyEye = flyEye + forward * (4.0f * delta * cosf(static_cast<float>(now) * 0.4f));
            }
            const Math::Vec3 moved = flyEye - previousEye;
            lastStep = moved.Length();
            previousEye = flyEye;
            lastKeys = (key_down(window, KEY_W) ? 1u : 0u) | (key_down(window, KEY_S) ? 2u : 0u) |
                       (key_down(window, KEY_A) ? 4u : 0u) | (key_down(window, KEY_D) ? 8u : 0u) |
                       (key_down(window, KEY_Q) ? 16u : 0u) | (key_down(window, KEY_E) ? 32u : 0u) |
                       (key_down(window, KEY_LEFT_SHIFT) ? 64u : 0u) |
                       (mouse_button_down(window, MOUSE_RIGHT) ? 128u : 0u);
            fov = model.camera.yfov;
            nearPlane = model.camera.nearPlane;
            farPlane = model.camera.farPlane < 400.0f ? model.camera.farPlane : 400.0f;
            eye = flyEye;
            view = Math::Mat4::LookAt(eye, eye + forward, Math::Vec3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            const float angle = 0.6f + 0.25f * time;
            const float distance = radius * 2.4f;
            eye = Math::Vec3(center.x + distance * sinf(angle), center.y + radius * 0.35f,
                    center.z + distance * cosf(angle));
            view = Math::Mat4::LookAt(eye, center, Math::Vec3(0.0f, 1.0f, 0.0f));
        }
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(fov, aspect, nearPlane, farPlane);
        const Math::Frustum frustum = Math::Frustum::FromViewProjection(
                Math::Mat4::Perspective(fov, aspect, nearPlane, farPlane) * view);

        const float pixelsPerUnit = static_cast<float>(height) * 0.5f / tanf(fov * 0.5f);
        draws.clear();
        for (const Candidate& candidate: candidates)
        {
            if (!frustum.IntersectsBox(candidate.box)) continue;
            const float distance = (candidate.center - eye).Length();
            if (minPixels > 0.0f)
            {
                const float pixels = candidate.diameter * pixelsPerUnit /
                                     (distance > nearPlane ? distance : nearPlane);
                if (pixels < minPixels) continue;
            }
            Draw draw = candidate.draw;
            draw.distance = distance;
            draws.push_back(draw);
        }
        if (draws.size() > 1)
            ct::sort(draws.data(), draws.data() + draws.size(), [](const Draw& a, const Draw& b) {
                if (a.blend != b.blend) return !a.blend;
                if (a.blend) return a.distance > b.distance;
                if (a.doubleSided != b.doubleSided) return !a.doubleSided;
                if (a.material != b.material) return a.material < b.material;
                return a.primitive < b.primitive;
            });

        stats.phase(0);
        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.inverseViewProjection = frame.viewProjection.Inverse();
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = exposure;
        frame.exposure[1] = 0.0f;
        frame.exposure[2] = iblScale;
        frame.exposure[3] = skyScale;

        unsigned triangles = 0;
        for (size_t i = 0; i < draws.size(); ++i)
            triangles += model.primitives[draws[i].primitive].indexCount / 3;
        visibleTriangles = triangles;

        depthOrder.clear();
        for (unsigned i = 0; usePrepass && i < draws.size(); ++i)
            if (!draws[i].blend) depthOrder.push_back(i);
        if (depthOrder.size() > 1)
            ct::sort(depthOrder.data(), depthOrder.data() + depthOrder.size(),
                    [&draws](unsigned a, unsigned b) {
                        const Draw& da = draws[a];
                        const Draw& db = draws[b];
                        const int ka = (da.mask ? 1 : 0) + (da.doubleSided ? 2 : 0);
                        const int kb = (db.mask ? 1 : 0) + (db.doubleSided ? 2 : 0);
                        if (ka != kb) return ka < kb;
                        return da.distance < db.distance;
                    });

        if (shadowSlots.count > 0 &&
                zenapp::chooseShadowLights(&lights, frustum, eye, shadowNear, shadowDistance,
                        &shadowSlots))
            lightsDirty = true;
        const bool lightsMoved = !clusteredValid || lightsDirty || width != clusteredWidth ||
                                 height != clusteredHeight ||
                                 memcmp(view.Data(), clusteredView, sizeof(clusteredView)) != 0 ||
                                 memcmp(projection.Data(), clusteredProjection,
                                         sizeof(clusteredProjection)) != 0;
        if (lightsMoved)
        {
            froxelizer.prepare(static_cast<unsigned>(width), static_cast<unsigned>(height),
                    projection.Data(), nearPlane, farPlane);
            zenapp::buildClusteredBuffers(lights, froxelizer, view.Data(), &clustered);
            memcpy(clusteredView, view.Data(), sizeof(clusteredView));
            memcpy(clusteredProjection, projection.Data(), sizeof(clusteredProjection));
            clusteredWidth = width;
            clusteredHeight = height;
            clusteredValid = true;
            lightsDirty = false;
        }

        stats.phase(1);
        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        if (lightsMoved) driver->updateBuffer(clusteredBuffer, 0, &clustered, sizeof(clustered));
        overlay.begin(static_cast<unsigned>(width), static_cast<unsigned>(height));
        if (overlayReady && showStats)
        {
            char extra[128];
            snprintf(extra, sizeof(extra), "%s  %uk tris  draws %u of %u  F1 hides",
                    driver->type() == prisma::DriverType::Vulkan ? "Vulkan" : "OpenGL",
                    triangles / 1000, static_cast<unsigned>(draws.size()), maxDraws);
            zenapp::drawStatsOverlay(&overlay, stats, phaseNames, 4, extra);
        }
        overlay.upload(driver);
        unsigned pendingSlots[2];
        unsigned pendingMaps[2];
        zenapp::ShadowMapView pendingViews[2][zenapp::ShadowSlots::kMapsPerSlot];
        unsigned pendingCount = 0;
        for (unsigned slot = 0; slot < shadowSlots.count && pendingCount < 2; ++slot)
        {
            if (shadowSlots.light[slot] < 0 || shadowSlots.drawn[slot]) continue;
            const unsigned light = static_cast<unsigned>(shadowSlots.light[slot]);
            const unsigned firstLayer = slot * zenapp::ShadowSlots::kMapsPerSlot;
            pendingSlots[pendingCount] = slot;
            pendingMaps[pendingCount] = zenapp::lightShadowMaps(lights.culling[light], light,
                    shadowNear, pendingViews[pendingCount]);
            for (unsigned face = 0; face < pendingMaps[pendingCount]; ++face)
            {
                const zenapp::ShadowMapView& map = pendingViews[pendingCount][face];
                shadowUniforms.matrices[firstLayer + face] = map.viewProjection;
                shadowUniforms.info[firstLayer + face][0] = map.texelScale;
                FrameUniforms shadowFrame;
                memset(&shadowFrame, 0, sizeof(shadowFrame));
                shadowFrame.viewProjection = map.viewProjection;
                memcpy(shadowFrameBytes.data() +
                                static_cast<size_t>(firstLayer + face) * frameStride,
                        &shadowFrame, sizeof(shadowFrame));
            }
            driver->updateBuffer(shadowFrameBuffer, firstLayer * frameStride,
                    shadowFrameBytes.data() + static_cast<size_t>(firstLayer) * frameStride,
                    zenapp::ShadowSlots::kMapsPerSlot * frameStride);
            ++pendingCount;
        }
        if (pendingCount > 0)
            driver->updateBuffer(shadowUniformBuffer, 0, &shadowUniforms, sizeof(shadowUniforms));

        if (gpuQuery.valid()) driver->beginQuery(gpuQuery);
        for (unsigned pending = 0; pending < pendingCount; ++pending)
        {
            const unsigned slot = pendingSlots[pending];
            const unsigned firstLayer = slot * zenapp::ShadowSlots::kMapsPerSlot;
            for (unsigned face = 0; face < pendingMaps[pending]; ++face)
            {
                const Math::Frustum lightFrustum = Math::Frustum::FromViewProjection(
                        pendingViews[pending][face].cullViewProjection);
                prisma::RenderPassDesc shadowPass;
                shadowPass.depth.texture = shadowMap;
                shadowPass.depth.layer = firstLayer + face;
                driver->beginRenderPass(shadowPass);
                driver->bindUniformBuffer(0, shadowFrameBuffer, (firstLayer + face) * frameStride,
                        sizeof(FrameUniforms));
                zenapp::bindGltfGeometry(driver, gpu);
                int boundKind = -1;
                for (const Candidate& candidate: candidates)
                {
                    const Draw& draw = candidate.draw;
                    if (draw.blend || !lightFrustum.IntersectsBox(candidate.box)) continue;
                    const int kind = (draw.mask ? 1 : 0) + (draw.doubleSided ? 2 : 0);
                    if (kind != boundKind)
                    {
                        driver->bindPipeline(shadowPipelines[kind]);
                        boundKind = kind;
                    }
                    if (draw.mask) zenapp::bindGltfMaterial(driver, gpu, model, draw.material);
                    driver->bindUniformBuffer(2, objectBuffer, draw.node * objectStride,
                            sizeof(ObjectUniforms));
                    zenapp::drawGltfPrimitive(driver, gpu, model.primitives[draw.primitive]);
                }
                driver->endRenderPass();
            }
            shadowSlots.drawn[slot] = true;
            lights.lights[shadowSlots.light[slot]].spot[3] = static_cast<float>(firstLayer + 1);
            lightsDirty = true;
        }
        driver->beginRenderPass(pass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        zenapp::bindIbl(driver, ibl);
        driver->bindUniformBuffer(3, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, cluster),
                sizeof(zenapp::ClusterUniforms));
        driver->bindUniformBuffer(4, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, lights),
                sizeof(clustered.lights));
        driver->bindUniformBuffer(5, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, froxels),
                sizeof(clustered.froxels));
        driver->bindUniformBuffer(6, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, records),
                sizeof(clustered.records));
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindUniformBuffer(8, shadowUniformBuffer, 0, sizeof(shadowUniforms));
        driver->bindTexture(7, shadowMap, shadowSampler);
        zenapp::bindGltfGeometry(driver, gpu);

        if (usePrepass)
        {
            int depthKind = -1;
            for (size_t k = 0; k < depthOrder.size(); ++k)
            {
                const unsigned i = depthOrder[k];
                const Draw& draw = draws[i];
                const int kind = (draw.mask ? 1 : 0) + (draw.doubleSided ? 2 : 0);
                if (kind != depthKind)
                {
                    driver->bindPipeline(depthPipelines[kind]);
                    depthKind = kind;
                }
                if (draw.mask) zenapp::bindGltfMaterial(driver, gpu, model, draw.material);
                driver->bindUniformBuffer(2, objectBuffer, draw.node * objectStride,
                        sizeof(ObjectUniforms));
                zenapp::drawGltfPrimitive(driver, gpu, model.primitives[draw.primitive]);
            }
        }

        int boundMaterial = -2;
        int boundPipeline = -1;
        for (size_t i = 0; i < draws.size() && !skipMain; ++i)
        {
            const Draw& draw = draws[i];
            const int pipelineKind = draw.blend ? 2 : (draw.doubleSided ? 1 : 0);
            if (pipelineKind != boundPipeline)
            {
                driver->bindPipeline(pipelineKind == 2   ? blendPipeline
                                     : pipelineKind == 1 ? doublePipeline
                                                         : opaquePipeline);
                boundPipeline = pipelineKind;
                boundMaterial = -2;
            }
            if (draw.material != boundMaterial)
            {
                zenapp::bindGltfMaterial(driver, gpu, model, draw.material);
                boundMaterial = draw.material;
            }
            driver->bindUniformBuffer(2, objectBuffer, draw.node * objectStride,
                    sizeof(ObjectUniforms));
            zenapp::drawGltfPrimitive(driver, gpu, model.primitives[draw.primitive]);
        }
        overlay.draw(driver);
        driver->endRenderPass();
        if (gpuQuery.valid()) driver->endQuery(gpuQuery);
        stats.phase(2);
        zenapp::endFrame(driver);
        driver->present();
        std::uint64_t nanoseconds = 0;
        if (gpuQuery.valid() && driver->queryResult(gpuQuery, &nanoseconds))
        {
            lastGpu = static_cast<float>(nanoseconds) * 1e-6f;
            stats.gpu(lastGpu);
        }
        stats.phase(3);
        stats.end();
        frameLog.add(stats, lastGpu, lastStep, lastKeys, visibleTriangles);

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    {
        int width = 0;
        int height = 0;
        window_get_framebuffer_size(window, &width, &height);
        char header[1536];
        snprintf(header, sizeof(header),
                "gltf_viewer backend=%s window=%dx%d vsync=%s prepass=%d warmup=%d camera=%d "
#ifdef NDEBUG
                "build=release "
#else
                "build=DEBUG "
#endif
                "model=%s skipmips=%u frames=%zu",
                driver->type() == prisma::DriverType::Vulkan ? "vulkan" : "opengl", width, height,
                zenapp::hasArgument(argc, argv, "novsync") ? "off" : "on", usePrepass ? 1 : 0,
                zenapp::hasArgument(argc, argv, "nowarmup") ? 0 : 1, fileCamera ? 1 : 0, modelPath,
                skipMips, frameLog.size());
        if (frameLog.write(reportPath, header)) log_info("report written to %s", reportPath);
    }
    overlay.destroy(driver);
    driver->destroy(gpuQuery);
    for (int i = 0; i < 4; ++i) driver->destroy(depthPipelines[i]);
    for (int i = 0; i < 4; ++i) driver->destroy(shadowPipelines[i]);
    driver->destroy(shadowFrameBuffer);
    driver->destroy(shadowUniformBuffer);
    driver->destroy(shadowSampler);
    driver->destroy(shadowMap);
    driver->destroy(skyPipeline);
    driver->destroy(blendPipeline);
    driver->destroy(doublePipeline);
    driver->destroy(opaquePipeline);
    driver->destroy(clusteredBuffer);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
