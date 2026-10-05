#include "common/Equirect.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/ObjModel.h"
#include "common/PbrTextures.h"
#include "common/PostProcess.h"
#include "common/Projection.h"
#include "common/Ssao.h"
#include "common/SunShadow.h"
#include "common/SunShadowFit.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "material.frag.h"
#include "material.vert.h"
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

struct MaterialUniforms
{
    float baseColor[4];
    float params[4];
    float flags[4];
};

struct MaterialDef
{
    const char* set;
    bool metallicMap;
    bool occlusionMap;
    const char* colorFile;
    const char* normalFile;
    float color[3];
    float metallic;
    float roughness;
    float uvScale;
    float normalScale;
};

const MaterialDef kMaterials[] = {
    { "Copper_tiles_01", true, true, nullptr, nullptr, { 1, 1, 1 }, 1.0f, 1.0f, 2.0f, 1.0f },
    { "Parquet_flooring_05", false, true, nullptr, nullptr, { 1, 1, 1 }, 0.0f, 1.0f, 2.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 1.0f, 0.766f, 0.336f }, 1.0f, 0.12f, 1.0f, 1.0f },
    { "Dirty_gold_01", true, true, nullptr, nullptr, { 1, 1, 1 }, 1.0f, 1.0f, 2.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.8f, 0.02f, 0.02f }, 0.0f, 0.08f, 1.0f, 1.0f },
    { "Blue_tiles_01", false, true, nullptr, nullptr, { 1, 1, 1 }, 0.0f, 1.0f, 2.0f, 1.0f },
    { "Metal_weave_01", true, false, nullptr, nullptr, { 1, 1, 1 }, 1.0f, 1.0f, 2.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.02f, 0.2f, 0.9f }, 0.0f, 0.08f, 1.0f, 1.0f },
    { "Moss_01", false, true, nullptr, nullptr, { 1, 1, 1 }, 0.0f, 1.0f, 2.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.955f, 0.638f, 0.538f }, 1.0f, 0.18f, 1.0f, 1.0f },
    { "Sandy_gravel_01", false, true, nullptr, nullptr, { 1, 1, 1 }, 0.0f, 1.0f, 2.0f, 1.0f },
    { nullptr, false, false, "fresnel/marble3.png", "fresnel/marble3_n.png", { 1, 1, 1 }, 0.0f, 0.22f, 1.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.95f, 0.95f, 0.95f }, 1.0f, 0.03f, 1.0f, 1.0f },
    { "Striped_cotton_01", false, true, nullptr, nullptr, { 1, 1, 1 }, 0.0f, 1.0f, 3.0f, 1.0f },
    { nullptr, false, false, "textures/bricks2.jpg", "textures/bricks2_normal.jpg", { 1, 1, 1 }, 0.0f, 0.8f, 2.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.05f, 0.6f, 0.1f }, 0.0f, 0.12f, 1.0f, 1.0f },
    { "Parquet_flooring_05", false, true, nullptr, nullptr, { 0.55f, 0.32f, 0.22f }, 0.0f, 1.0f, 1.5f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.91f, 0.92f, 0.92f }, 1.0f, 0.28f, 1.0f, 1.0f },
    { nullptr, false, false, "fresnel/marble1.jpg", "fresnel/marble1_n.png", { 1, 1, 1 }, 0.0f, 0.2f, 1.0f, 1.0f },
    { "Metal_weave_01", true, false, nullptr, nullptr, { 0.8f, 0.55f, 0.35f }, 1.0f, 1.0f, 3.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.02f, 0.02f, 0.02f }, 0.0f, 0.85f, 1.0f, 1.0f },
    { "Moss_01", false, true, nullptr, nullptr, { 1.1f, 1.0f, 0.5f }, 0.0f, 1.0f, 3.0f, 1.0f },
    { nullptr, false, false, "fresnel/marble2.jpg", nullptr, { 1, 1, 1 }, 0.0f, 0.25f, 1.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.9f, 0.35f, 0.05f }, 0.0f, 0.7f, 1.0f, 1.0f },
    { "Sandy_gravel_01", false, true, nullptr, nullptr, { 1.0f, 0.7f, 0.6f }, 0.0f, 1.0f, 3.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.9f, 0.9f, 0.88f }, 0.0f, 0.15f, 1.0f, 1.0f },
    { "Blue_tiles_01", false, true, nullptr, nullptr, { 1.2f, 0.7f, 0.5f }, 0.0f, 1.0f, 3.0f, 1.0f },
    { "Copper_tiles_01", true, true, nullptr, nullptr, { 0.8f, 1.0f, 0.9f }, 1.0f, 1.0f, 3.0f, 1.0f },
    { nullptr, false, false, nullptr, nullptr, { 0.56f, 0.57f, 0.58f }, 1.0f, 0.4f, 1.0f, 1.0f },
    { "Striped_cotton_01", false, true, nullptr, nullptr, { 0.6f, 0.8f, 1.2f }, 0.0f, 1.0f, 3.0f, 1.0f },
};
const unsigned kMaterialCount = sizeof(kMaterials) / sizeof(kMaterials[0]);

const unsigned kColumns = 6;
const unsigned kRows = 5;
const float kSpacing = 3.3f;
const unsigned kRenderWidth = 1280;
const unsigned kRenderHeight = 720;
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
    const float blur = numberArgument(argc, argv, "blur", 2.0f);
    const float ev = numberArgument(argc, argv, "ev", -0.3f);
    const float divisor = numberArgument(argc, argv, "texdiv", 2.0f);
    const float cameraHeight = numberArgument(argc, argv, "height", 6.5f);
    const float cameraDistance = numberArgument(argc, argv, "distance", 9.0f);
    const float swing = numberArgument(argc, argv, "swing", 0.3f);
    const float bloomStrength = numberArgument(argc, argv, "bloom", 0.10f);
    const bool fxaaEnabled = numberArgument(argc, argv, "fxaa", 1.0f) > 0.5f;
    const bool ssaoEnabled = numberArgument(argc, argv, "ssao", 1.0f) > 0.5f;
    const bool ssaoDebug = zenapp::hasArgument(argc, argv, "aodebug");
    const float sunScale = numberArgument(argc, argv, "sun", 1.0f);
    const unsigned shadowSize = static_cast<unsigned>(numberArgument(argc, argv, "shadowsize", 2048.0f));

    char modelPath[1024];
    snprintf(modelPath, sizeof(modelPath), "%s/shader_ball/shader_ball.obj", PRISMA_MODELS_DIR);
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath), "%s/../environments/parking_garage_2k.hdr",
                PRISMA_MODELS_DIR);
    char texturesRoot[1024];
    snprintf(texturesRoot, sizeof(texturesRoot), "%s/../textures", PRISMA_MODELS_DIR);
    const char* extraArgument = zenapp::argumentValue(argc, argv, "extra");
    const char* extraRoot = extraArgument ? extraArgument : "/media/projectos/projects/cpp/radion-examples/assets";

    zenapp::GltfModel model;
    if (!zenapp::loadObj(modelPath, &model, true))
    {
        log_error("materials: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    const unsigned ballPrimitives = static_cast<unsigned>(model.primitives.size());
    Math::Vec3 ballLow(1e30f, 1e30f, 1e30f);
    for (unsigned p = 0; p < ballPrimitives; ++p)
        ballLow.y = fminf(ballLow.y, model.primitives[p].boundsMin[1]);

    {
        const float half = 400.0f;
        zenapp::GltfPrimitive floor;
        floor.firstVertex = static_cast<uint32_t>(model.vertices.size());
        floor.vertexCount = 4;
        floor.firstIndex = static_cast<uint32_t>(model.indices.size());
        floor.indexCount = 6;
        floor.material = -1;
        floor.hasTangents = true;
        const float corners[4][2] = { { -half, -half }, { half, -half }, { half, half }, { -half, half } };
        for (int i = 0; i < 4; ++i)
        {
            zenapp::GltfVertex vertex;
            memset(&vertex, 0, sizeof(vertex));
            vertex.position[0] = corners[i][0];
            vertex.position[2] = corners[i][1];
            vertex.normal[1] = 1.0f;
            vertex.tangent[0] = 1.0f;
            vertex.tangent[3] = 1.0f;
            vertex.uv[0] = corners[i][0] / 40.0f;
            vertex.uv[1] = corners[i][1] / 40.0f;
            model.vertices.push_back(vertex);
        }
        const uint32_t order[6] = { 0, 3, 2, 0, 2, 1 };
        for (int i = 0; i < 6; ++i) model.indices.push_back(order[i]);
        model.primitives.push_back(floor);
    }
    const unsigned floorPrimitive = ballPrimitives;

    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("materials: cannot read %s", environmentPath);
        return 1;
    }

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 35 materials", driverType);
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
        log_error("materials: this GPU cannot render to float textures");
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

    const unsigned total = kMaterialCount + 1;
    ct::Vector<zenapp::PbrTextureSet> sets;
    sets.resize(total);
    ct::Vector<int> ownerOf;
    ownerOf.resize(total);
    ct::Vector<MaterialUniforms> uniforms;
    uniforms.resize(total);
    unsigned loadedSets = 0;
    for (unsigned m = 0; m < total; ++m)
    {
        const bool isFloor = m == kMaterialCount;
        const MaterialDef floorDef = { "Sandy_gravel_01", false, true, nullptr, nullptr,
            { 0.2f, 0.17f, 0.13f }, 0.0f, 1.0f, 6.0f, 1.0f };
        const MaterialDef& def = isFloor ? floorDef : kMaterials[m];
        ownerOf[m] = -1;
        if (def.set)
        {
            for (unsigned k = 0; k < m && ownerOf[m] < 0; ++k)
            {
                const MaterialDef& other = k == kMaterialCount ? floorDef : kMaterials[k];
                if (other.set && strcmp(other.set, def.set) == 0 && ownerOf[k] < 0 &&
                        (sets[k].base.valid() || sets[k].orm.valid()))
                    ownerOf[m] = static_cast<int>(k);
            }
            if (ownerOf[m] >= 0)
                sets[m] = sets[static_cast<size_t>(ownerOf[m])];
            else
            {
                char color[1100], normal[1100], ao[1100], roughness[1100], metallic[1100];
                snprintf(color, sizeof(color), "%s/%s/%s_Color.png", texturesRoot, def.set, def.set);
                snprintf(normal, sizeof(normal), "%s/%s/%s_Normal.png", texturesRoot, def.set, def.set);
                snprintf(ao, sizeof(ao), "%s/%s/%s_AO.png", texturesRoot, def.set, def.set);
                snprintf(roughness, sizeof(roughness), "%s/%s/%s_Roughness.png", texturesRoot, def.set,
                        def.set);
                snprintf(metallic, sizeof(metallic), "%s/%s/%s_Metallic.png", texturesRoot, def.set,
                        def.set);
                if (!zenapp::loadPbrSet(driver, color, normal, def.occlusionMap ? ao : nullptr, roughness,
                            def.metallicMap ? metallic : nullptr, static_cast<unsigned>(divisor), &sets[m]))
                    log_error("materials: cannot load the texture set %s", def.set);
                else
                    ++loadedSets;
            }
        }
        else if (def.colorFile)
        {
            char color[1100], normal[1100];
            snprintf(color, sizeof(color), "%s/%s", extraRoot, def.colorFile);
            snprintf(normal, sizeof(normal), "%s/%s", extraRoot, def.normalFile ? def.normalFile : "");
            if (!zenapp::loadPbrSet(driver, color, def.normalFile ? normal : nullptr, nullptr, nullptr, nullptr,
                        1, &sets[m]))
                log_warn("materials: cannot load %s, using its colour", color);
        }
        MaterialUniforms& u = uniforms[m];
        memset(&u, 0, sizeof(u));
        for (int i = 0; i < 3; ++i) u.baseColor[i] = def.color[i];
        u.baseColor[3] = 1.0f;
        u.params[0] = def.metallic;
        u.params[1] = def.roughness;
        u.params[2] = def.uvScale;
        u.params[3] = def.normalScale;
        u.flags[0] = sets[m].base.valid() ? 1.0f : 0.0f;
        u.flags[1] = sets[m].normal.valid() ? 1.0f : 0.0f;
        u.flags[2] = sets[m].orm.valid() ? 1.0f : 0.0f;
        u.flags[3] = isFloor ? 0.6f : 1.0f;
    }
    log_info("materials: %u texture sets loaded, %u materials", loadedSets, kMaterialCount);

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    const unsigned char flatPixel[4] = { 128, 128, 255, 255 };
    prisma::TextureDesc pixelDesc;
    pixelDesc.width = 1;
    pixelDesc.height = 1;
    pixelDesc.data = whitePixel;
    pixelDesc.debugName = "white";
    const prisma::TextureHandle whiteTexture = driver->createTexture(pixelDesc);
    pixelDesc.data = flatPixel;
    pixelDesc.debugName = "flat normal";
    const prisma::TextureHandle flatTexture = driver->createTexture(pixelDesc);
    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy = 8.0f;
    samplerDesc.debugName = "material sampler";
    const prisma::SamplerHandle materialSampler = driver->createSampler(samplerDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned objectStride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
    const unsigned materialStride = (sizeof(MaterialUniforms) + alignment - 1) / alignment * alignment;
    const unsigned instanceCount = kColumns * kRows;
    ct::Vector<unsigned char> objectBytes;
    objectBytes.resize(static_cast<size_t>(objectStride) * (instanceCount + 1));
    memset(objectBytes.data(), 0, objectBytes.size());
    for (unsigned j = 0; j < kRows; ++j)
    {
        for (unsigned i = 0; i < kColumns; ++i)
        {
            ObjectUniforms object;
            const float x = (static_cast<float>(i) - (kColumns - 1) * 0.5f) * kSpacing +
                            ((j & 1) ? kSpacing * 0.5f : -kSpacing * 0.25f);
            const float z = -static_cast<float>(j) * kSpacing;
            object.model = Math::Mat4::Translation(Math::Vec3(x, -ballLow.y, z));
            object.normalMatrix = object.model.Inverse().Transposed();
            memcpy(objectBytes.data() + static_cast<size_t>(j * kColumns + i) * objectStride, &object,
                    sizeof(object));
        }
    }
    {
        ObjectUniforms object;
        object.model = Math::Mat4::Identity();
        object.normalMatrix = Math::Mat4::Identity();
        memcpy(objectBytes.data() + static_cast<size_t>(instanceCount) * objectStride, &object, sizeof(object));
    }
    ct::Vector<unsigned char> materialBytes;
    materialBytes.resize(static_cast<size_t>(materialStride) * total);
    memset(materialBytes.data(), 0, materialBytes.size());
    for (unsigned m = 0; m < total; ++m)
        memcpy(materialBytes.data() + static_cast<size_t>(m) * materialStride, &uniforms[m], sizeof(MaterialUniforms));

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
    prisma::BufferDesc materialDesc;
    materialDesc.usage = prisma::BufferUsage::Uniform;
    materialDesc.size = static_cast<std::uint32_t>(materialBytes.size());
    materialDesc.data = materialBytes.data();
    materialDesc.debugName = "material uniforms";
    const prisma::BufferHandle materialBuffer = driver->createBuffer(materialDesc);

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

    const prisma::ShaderHandle materialVertex = zenapp::createShader(driver, material_vert);
    const prisma::ShaderHandle materialFragment = zenapp::createShader(driver, material_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc materialPipelineDesc;
    materialPipelineDesc.vertexShader = materialVertex;
    materialPipelineDesc.fragmentShader = materialFragment;
    materialPipelineDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
    materialPipelineDesc.vertexBufferCount = 1;
    materialPipelineDesc.attributeCount = 4;
    materialPipelineDesc.attributes[0].location = 0;
    materialPipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    materialPipelineDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
    materialPipelineDesc.attributes[1].location = 1;
    materialPipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    materialPipelineDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, normal);
    materialPipelineDesc.attributes[2].location = 2;
    materialPipelineDesc.attributes[2].format = prisma::VertexFormat::Float4;
    materialPipelineDesc.attributes[2].offset = offsetof(zenapp::GltfVertex, tangent);
    materialPipelineDesc.attributes[3].location = 3;
    materialPipelineDesc.attributes[3].format = prisma::VertexFormat::Float2;
    materialPipelineDesc.attributes[3].offset = offsetof(zenapp::GltfVertex, uv);
    materialPipelineDesc.depthTest = true;
    materialPipelineDesc.cullMode = prisma::CullMode::Back;
    setTargetFormats(&materialPipelineDesc, true);
    materialPipelineDesc.debugName = "material";
    const prisma::PipelineHandle materialPipeline = driver->createPipeline(materialPipelineDesc);
    prisma::PipelineDesc floorPipelineDesc = materialPipelineDesc;
    floorPipelineDesc.cullMode = prisma::CullMode::None;
    floorPipelineDesc.debugName = "floor";
    const prisma::PipelineHandle floorPipeline = driver->createPipeline(floorPipelineDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    setTargetFormats(&skyDesc, true);
    skyDesc.debugName = "sky";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(materialVertex);
    driver->destroy(materialFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    zenapp::PostProcess post;
    const bool postReady = zenapp::createPostProcess(driver, kRenderWidth, kRenderHeight,
            bloomStrength, fxaaEnabled, &post);

    const float aoNear = 0.1f;
    const float aoFar = 200.0f;
    const float aspect = static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight);
    const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, aoNear, aoFar);
    const Math::Mat4 inverseProjection = projection.Inverse();
    zenapp::SsaoDesc ssaoDesc;
    ssaoDesc.width = kRenderWidth;
    ssaoDesc.height = kRenderHeight;
    ssaoDesc.projection = projection.Data();
    ssaoDesc.inverseProjection = inverseProjection.Data();
    ssaoDesc.farPlane = aoFar;
    ssaoDesc.enabled = ssaoEnabled;
    ssaoDesc.debug = ssaoDebug;
    zenapp::Ssao ssao;
    const bool ssaoReady = zenapp::createSsao(driver, ssaoDesc, &ssao);

    Math::Vec3 sunLow(1e30f, 0.0f, 1e30f);
    Math::Vec3 sunHigh(-1e30f, 3.2f, -1e30f);
    for (unsigned j = 0; j < kRows; ++j)
    {
        for (unsigned i = 0; i < kColumns; ++i)
        {
            const float x = (static_cast<float>(i) - (kColumns - 1) * 0.5f) * kSpacing +
                            ((j & 1) ? kSpacing * 0.5f : -kSpacing * 0.25f);
            const float z = -static_cast<float>(j) * kSpacing;
            sunLow.x = fminf(sunLow.x, x - 2.0f);
            sunLow.z = fminf(sunLow.z, z - 2.0f);
            sunHigh.x = fmaxf(sunHigh.x, x + 2.0f);
            sunHigh.z = fmaxf(sunHigh.z, z + 2.0f);
        }
    }
    const float toSunRaw[3] = { -0.6f, 1.0f, 0.5f };
    const float toSunLength = sqrtf(toSunRaw[0] * toSunRaw[0] + toSunRaw[1] * toSunRaw[1] +
                                    toSunRaw[2] * toSunRaw[2]);
    const Math::Vec3 toSun(toSunRaw[0] / toSunLength, toSunRaw[1] / toSunLength,
            toSunRaw[2] / toSunLength);
    zenapp::SunShadowParams sunParams;
    zenapp::fitSunShadow(sunLow, sunHigh, toSun, shadowSize, 1.0f, &sunParams);
    zenapp::SunShadow sunShadow;
    const bool sunShadowReady = zenapp::createSunShadow(driver, shadowSize, sunParams, &sunShadow);

    const bool ready = sunShadowReady && ssaoReady && postReady && iblReady && gpuReady && whiteTexture.valid() && flatTexture.valid() &&
                       materialSampler.valid() && frameBuffer.valid() && objectBuffer.valid() &&
                       materialBuffer.valid() && sceneColor.valid() &&
                       sceneDepth.valid() && materialPipeline.valid() && floorPipeline.valid() &&
                       skyPipeline.valid();
    if (!ready) log_error("materials: resource creation failed");

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

        const float sway = still ? 0.0f : sinf(static_cast<float>(time_seconds()) * 0.25f) * swing;
        const Math::Vec3 target(0.0f, 1.2f, -static_cast<float>(kRows - 1) * kSpacing * 0.5f);
        const Math::Vec3 eye(target.x + sinf(sway) * cameraDistance, cameraHeight,
                target.z + cosf(sway) * cameraDistance);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, target, Math::Vec3(0.0f, 1.0f, 0.0f));

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
        frame.sunDirection[0] = toSun.x;
        frame.sunDirection[1] = toSun.y;
        frame.sunDirection[2] = toSun.z;
        frame.sunColorIntensity[0] = frame.sunColorIntensity[1] = frame.sunColorIntensity[2] = 1.0f;
        frame.sunColorIntensity[3] = kSunLux * exposure * sunScale;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        zenapp::renderSunShadow(driver, &sunShadow, [&](prisma::PipelineHandle) {
            zenapp::bindGltfGeometry(driver, gpu);
            for (unsigned instance = 0; instance < instanceCount; ++instance)
            {
                driver->bindUniformBuffer(2, objectBuffer, instance * objectStride, sizeof(ObjectUniforms));
                for (unsigned p = 0; p < ballPrimitives; ++p)
                    zenapp::drawGltfPrimitive(driver, gpu, model.primitives[p]);
            }
        });

        zenapp::renderSsao(driver, ssao, [&](prisma::PipelineHandle single, prisma::PipelineHandle doubled) {
            zenapp::bindGltfGeometry(driver, gpu);
            for (unsigned instance = 0; instance <= instanceCount; ++instance)
            {
                const bool isFloor = instance == instanceCount;
                driver->bindPipeline(isFloor ? doubled : single);
                driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
                driver->bindUniformBuffer(2, objectBuffer, instance * objectStride, sizeof(ObjectUniforms));
                if (isFloor)
                    zenapp::drawGltfPrimitive(driver, gpu, model.primitives[floorPrimitive]);
                else
                    for (unsigned p = 0; p < ballPrimitives; ++p)
                        zenapp::drawGltfPrimitive(driver, gpu, model.primitives[p]);
            }
        });

        driver->beginRenderPass(scenePass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        zenapp::bindGltfGeometry(driver, gpu);
        for (unsigned instance = 0; instance <= instanceCount; ++instance)
        {
            const bool isFloor = instance == instanceCount;
            const unsigned materialIndex = isFloor ? kMaterialCount : instance % kMaterialCount;
            const zenapp::PbrTextureSet& set = sets[materialIndex];
            driver->bindPipeline(isFloor ? floorPipeline : materialPipeline);
            driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
            zenapp::bindIbl(driver, ibl);
            zenapp::bindSsao(driver, ssao);
            zenapp::bindSunShadow(driver, sunShadow);
            driver->bindUniformBuffer(2, objectBuffer, instance * objectStride, sizeof(ObjectUniforms));
            driver->bindUniformBuffer(7, materialBuffer, materialIndex * materialStride, sizeof(MaterialUniforms));
            driver->bindTexture(2, set.base.valid() ? set.base : whiteTexture, materialSampler);
            driver->bindTexture(3, set.normal.valid() ? set.normal : flatTexture, materialSampler);
            driver->bindTexture(4, set.orm.valid() ? set.orm : whiteTexture, materialSampler);
            if (isFloor)
                zenapp::drawGltfPrimitive(driver, gpu, model.primitives[floorPrimitive]);
            else
                for (unsigned p = 0; p < ballPrimitives; ++p)
                    zenapp::drawGltfPrimitive(driver, gpu, model.primitives[p]);
        }
        driver->endRenderPass();

        zenapp::renderPostProcess(driver, post, sceneColor, windowPass);
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(floorPipeline);
    driver->destroy(materialPipeline);
    driver->destroy(sceneDepth);
    driver->destroy(sceneColor);
    zenapp::destroySunShadow(driver, &sunShadow);
    zenapp::destroySsao(driver, &ssao);
    zenapp::destroyPostProcess(driver, &post);
    driver->destroy(materialBuffer);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    driver->destroy(materialSampler);
    driver->destroy(flatTexture);
    driver->destroy(whiteTexture);
    for (unsigned m = 0; m < total; ++m)
    {
        if (ownerOf[m] >= 0) continue;
        driver->destroy(sets[m].base);
        driver->destroy(sets[m].normal);
        driver->destroy(sets[m].orm);
    }
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
