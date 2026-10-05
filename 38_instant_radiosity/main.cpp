#include "common/Ibl.h"
#include "common/Lights.h"
#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsm.frag.h"
#include "scene.frag.h"
#include "scene.vert.h"
#include "vpl.comp.h"

namespace
{

const std::uint32_t kMapSize = 256;
const unsigned kMaxVpls = 224;
const unsigned kGroupSize = 64;
const float kRoomHalf = 2.0f;
const float kRoomHeight = 4.0f;
const float kDwarfHeight = 1.8f;
const float kSpotInner = 0.55f;
const float kSpotOuter = 1.05f;
const float kStillTime = 2.0f;

// the draws of the room, the same in the pass of the map and in the final one
enum Part
{
    kPartWhite,
    kPartRed,
    kPartGreen,
    kPartBox,
    kPartDwarf,
    kPartCount
};

struct SceneVertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    float camera[4];
    float exposure[4];
};

struct ObjectUniforms
{
    Math::Mat4 model;
    float tint[4];
    float material[4];
};

struct DirectUniforms
{
    zenapp::LightData light;
    Math::Mat4 lightViewProjection;
    float shadow[4];
};

struct VplParams
{
    float params[4];
    float color[4];
};

struct Vpl
{
    float position[4];
    float normal[4];
    float flux[4];
};

void bounds(const zenapp::SdkMesh& mesh, Math::Vec3* low, Math::Vec3* high)
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

bool sceneLayout(const zenapp::SdkMesh& mesh)
{
    prisma::PipelineDesc desc;
    if (!mesh.layout(0, &desc)) return false;
    if (desc.vertexBufferCount != 1 || desc.vertexBuffers[0].stride != sizeof(SceneVertex))
        return false;
    for (unsigned i = 0; i < desc.attributeCount; ++i)
    {
        const prisma::VertexAttribute& a = desc.attributes[i];
        if (a.location == 0 && (a.offset != 0 || a.format != prisma::VertexFormat::Float3))
            return false;
        if (a.location == 1 && (a.offset != 12 || a.format != prisma::VertexFormat::Float3))
            return false;
        if (a.location == 2 && (a.offset != 24 || a.format != prisma::VertexFormat::Float2))
            return false;
    }
    return true;
}

// Two triangles facing the side of across x up.
void addQuad(SceneVertex* out, const Math::Vec3& origin, const Math::Vec3& across,
        const Math::Vec3& up)
{
    const float u[6] = { 0, 1, 1, 0, 1, 0 };
    const float v[6] = { 0, 0, 1, 0, 1, 1 };
    Math::Vec3 normal = across.Cross(up);
    normal = normal * (1.0f / normal.Length());
    for (int i = 0; i < 6; ++i)
    {
        const Math::Vec3 p = origin + across * u[i] + up * v[i];
        out[i].position[0] = p.x;
        out[i].position[1] = p.y;
        out[i].position[2] = p.z;
        out[i].normal[0] = normal.x;
        out[i].normal[1] = normal.y;
        out[i].normal[2] = normal.z;
        out[i].uv[0] = u[i];
        out[i].uv[1] = 1.0f - v[i];
    }
}

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

float argumentFloat(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

float maxChannel(const float* c)
{
    return c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    bool showMarkers = zenapp::hasArgument(argc, argv, "markers");
    bool indirectOn = !zenapp::hasArgument(argc, argv, "noindirect");
    unsigned vplCount = static_cast<unsigned>(argumentFloat(argc, argv, "vpls", 96.0f));
    vplCount = vplCount < 1 ? 1 : (vplCount > kMaxVpls ? kMaxVpls : vplCount);
    const float directScale = argumentFloat(argc, argv, "direct", 1.0f);
    const float indirectScale = argumentFloat(argc, argv, "indirect", 2.2f);
    const float exposure = argumentFloat(argc, argv, "exposure", 1.0f);
    const float ambient = argumentFloat(argc, argv, "ambient", 0.02f);
    const float cullThreshold = argumentFloat(argc, argv, "cull", 0.004f);
    const float lightIntensity = argumentFloat(argc, argv, "intensity", 40.0f);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 38 instant radiosity", driverType);
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

    if (!driver->caps().compute || !driver->caps().floatColorTargets)
    {
        log_error("instant radiosity: this GPU needs compute shaders and float render targets");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath("Light Probes/uffizi_cross.dds", path, sizeof(path));
    zenapp::EnvironmentFaces faces;
    bool ready = zenapp::loadEnvironmentFaces(path, &faces);
    if (!ready) log_error("instant radiosity: cannot read %s", path);
    zenapp::Ibl ibl;
    if (ready && !zenapp::createIbl(driver, faces, &ibl, ambient))
    {
        log_error("instant radiosity: cannot build the image based lighting resources");
        ready = false;
    }

    zenapp::mediaPath("Dwarf/dwarf.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh dwarf;
    const bool dwarfLoaded = dwarf.load(driver, path) && dwarf.meshCount() == 1 && sceneLayout(dwarf);
    if (!dwarfLoaded) log_error("dwarf: cannot load %s", path);
    ready = ready && dwarfLoaded;

    zenapp::mediaPath("misc/ball.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh ball;
    const bool ballLoaded = ball.load(driver, path, false) && ball.meshCount() == 1 && sceneLayout(ball);
    if (!ballLoaded) log_error("ball: cannot load %s", path);
    ready = ready && ballLoaded;

    Math::Vec3 low(0.0f, 0.0f, 0.0f);
    Math::Vec3 high(1.0f, 1.0f, 1.0f);
    Math::Vec3 ballLow(-1.0f, -1.0f, -1.0f);
    Math::Vec3 ballHigh(1.0f, 1.0f, 1.0f);
    if (ready)
    {
        bounds(dwarf, &low, &high);
        bounds(ball, &ballLow, &ballHigh);
    }
    const float extent = high.y - low.y;
    const float scale = extent > 0.0f ? kDwarfHeight / extent : 1.0f;
    const Math::Vec3 center = (low + high) * 0.5f;
    const Math::Mat4 dwarfModel = Math::Mat4::Translation(Math::Vec3(0.55f, 0.0f, 0.35f)) *
                                  Math::Mat4::RotationY(3.14159265f + 0.35f) *
                                  Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                                  Math::Mat4::Translation(Math::Vec3(-center.x, -low.y, -center.z));
    const Math::Vec3 ballCenter = (ballLow + ballHigh) * 0.5f;
    const Math::Vec3 ballSize = (ballHigh - ballLow) * 0.5f;
    const float ballExtent = ballSize.x > ballSize.y ? ballSize.x : ballSize.y;
    const float ballScale = ballExtent > 0.0f ? 0.05f / ballExtent : 1.0f;

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
    samplerDesc.debugName = "albedo sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);
    samplerDesc = prisma::SamplerDesc();
    samplerDesc.minFilter = prisma::Filter::Nearest;
    samplerDesc.magFilter = prisma::Filter::Nearest;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "map sampler";
    const prisma::SamplerHandle mapSampler = driver->createSampler(samplerDesc);
    samplerDesc.minFilter = prisma::Filter::Linear;
    samplerDesc.magFilter = prisma::Filter::Linear;
    samplerDesc.compare = true;
    samplerDesc.compareOp = prisma::CompareOp::LessEqual;
    samplerDesc.debugName = "shadow sampler";
    const prisma::SamplerHandle shadowSampler = driver->createSampler(samplerDesc);

    // the room: floor, ceiling and back wall (white), left wall (red), right wall (green), then a
    // box of five faces
    const float h = kRoomHalf;
    const float t = kRoomHeight;
    SceneVertex geometry[6 * 10];
    addQuad(geometry + 0, Math::Vec3(-h, 0, h), Math::Vec3(2 * h, 0, 0), Math::Vec3(0, 0, -2 * h));
    addQuad(geometry + 6, Math::Vec3(-h, t, -h), Math::Vec3(2 * h, 0, 0), Math::Vec3(0, 0, 2 * h));
    addQuad(geometry + 12, Math::Vec3(-h, 0, -h), Math::Vec3(2 * h, 0, 0), Math::Vec3(0, t, 0));
    addQuad(geometry + 18, Math::Vec3(-h, 0, h), Math::Vec3(0, 0, -2 * h), Math::Vec3(0, t, 0));
    addQuad(geometry + 24, Math::Vec3(h, 0, -h), Math::Vec3(0, 0, 2 * h), Math::Vec3(0, t, 0));
    {
        const float x0 = -1.5f;
        const float x1 = -0.5f;
        const float z0 = -1.4f;
        const float z1 = -0.4f;
        const float y1 = 1.3f;
        SceneVertex* box = geometry + 30;
        addQuad(box + 0, Math::Vec3(x0, y1, z1), Math::Vec3(x1 - x0, 0, 0), Math::Vec3(0, 0, z0 - z1));
        addQuad(box + 6, Math::Vec3(x0, 0, z1), Math::Vec3(x1 - x0, 0, 0), Math::Vec3(0, y1, 0));
        addQuad(box + 12, Math::Vec3(x1, 0, z0), Math::Vec3(x0 - x1, 0, 0), Math::Vec3(0, y1, 0));
        addQuad(box + 18, Math::Vec3(x0, 0, z0), Math::Vec3(0, 0, z1 - z0), Math::Vec3(0, y1, 0));
        addQuad(box + 24, Math::Vec3(x1, 0, z1), Math::Vec3(0, 0, z0 - z1), Math::Vec3(0, y1, 0));
    }
    const unsigned geometryVertices = 60;
    // the vertex ranges of the parts that are no mesh
    const unsigned partFirst[4] = { 0, 18, 24, 30 };
    const unsigned partCount[4] = { 18, 6, 6, 30 };
    const float partTint[kPartCount][4] = { { 0.76f, 0.75f, 0.72f, 1.0f },
        { 0.78f, 0.09f, 0.07f, 1.0f }, { 0.10f, 0.55f, 0.12f, 1.0f },
        { 0.76f, 0.75f, 0.72f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };
    const float partRoughness[kPartCount] = { 0.85f, 0.85f, 0.85f, 0.7f, 0.55f };

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(geometry);
    bufferDesc.data = geometry;
    bufferDesc.debugName = "room vertices";
    const prisma::BufferHandle geometryBuffer = driver->createBuffer(bufferDesc);

    // the map of what the light sees
    prisma::TextureDesc mapDesc;
    mapDesc.width = kMapSize;
    mapDesc.height = kMapSize;
    mapDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    mapDesc.format = prisma::TextureFormat::RGBA32F;
    mapDesc.debugName = "rsm position";
    const prisma::TextureHandle mapPosition = driver->createTexture(mapDesc);
    mapDesc.format = prisma::TextureFormat::RGBA16F;
    mapDesc.debugName = "rsm normal";
    const prisma::TextureHandle mapNormal = driver->createTexture(mapDesc);
    mapDesc.debugName = "rsm albedo";
    const prisma::TextureHandle mapAlbedo = driver->createTexture(mapDesc);
    mapDesc.format = prisma::TextureFormat::Depth32F;
    mapDesc.debugName = "rsm depth";
    const prisma::TextureHandle mapDepth = driver->createTexture(mapDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const std::uint32_t frameStride = alignUp(sizeof(FrameUniforms), alignment);
    const std::uint32_t objectStride = alignUp(sizeof(ObjectUniforms), alignment);
    const std::uint32_t objectBase = frameStride * 2;
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(static_cast<size_t>(objectBase) + objectStride * kPartCount);
    memset(uniforms.data(), 0, uniforms.size());
    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(uniforms.size());
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    ct::Vector<unsigned char> markerBytes;
    markerBytes.resize(static_cast<size_t>(objectStride) * kMaxVpls);
    memset(markerBytes.data(), 0, markerBytes.size());
    bufferDesc.size = static_cast<std::uint32_t>(markerBytes.size());
    bufferDesc.debugName = "markers";
    const prisma::BufferHandle markerBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.size = sizeof(VplParams);
    bufferDesc.debugName = "vpl params";
    const prisma::BufferHandle paramsBuffer = driver->createBuffer(bufferDesc);
    bufferDesc.size = sizeof(DirectUniforms);
    bufferDesc.debugName = "direct light";
    const prisma::BufferHandle directBuffer = driver->createBuffer(bufferDesc);

    static zenapp::ClusteredBuffers clustered;
    zenapp::Froxelizer froxelizer;
    froxelizer.setDepthRange(2.0f, 14.0f);
    bufferDesc.size = sizeof(clustered);
    bufferDesc.debugName = "clustered lights";
    const prisma::BufferHandle clusteredBuffer = driver->createBuffer(bufferDesc);

    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = sizeof(Vpl) * kMaxVpls;
    bufferDesc.debugName = "virtual point lights";
    const prisma::BufferHandle vplBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle sceneVertex = zenapp::createShader(driver, scene_vert);
    const prisma::ShaderHandle sceneFragment = zenapp::createShader(driver, scene_frag);
    const prisma::ShaderHandle rsmFragment = zenapp::createShader(driver, rsm_frag);
    const prisma::ShaderHandle vplShader = zenapp::createShader(driver, vpl_comp);

    prisma::PipelineDesc sceneDesc;
    sceneDesc.vertexShader = sceneVertex;
    sceneDesc.fragmentShader = sceneFragment;
    sceneDesc.vertexBuffers[0].stride = sizeof(SceneVertex);
    sceneDesc.vertexBufferCount = 1;
    sceneDesc.attributeCount = 3;
    sceneDesc.attributes[0].location = 0;
    sceneDesc.attributes[0].format = prisma::VertexFormat::Float3;
    sceneDesc.attributes[0].offset = 0;
    sceneDesc.attributes[1].location = 1;
    sceneDesc.attributes[1].format = prisma::VertexFormat::Float3;
    sceneDesc.attributes[1].offset = sizeof(float) * 3;
    sceneDesc.attributes[2].location = 2;
    sceneDesc.attributes[2].format = prisma::VertexFormat::Float2;
    sceneDesc.attributes[2].offset = sizeof(float) * 6;
    sceneDesc.depthTest = true;
    sceneDesc.cullMode = prisma::CullMode::Back;
    sceneDesc.debugName = "scene";
    const prisma::PipelineHandle scenePipeline = driver->createPipeline(sceneDesc);

    sceneDesc.fragmentShader = rsmFragment;
    sceneDesc.targets.window = false;
    sceneDesc.targets.colorCount = 3;
    sceneDesc.targets.colors[0] = prisma::TextureFormat::RGBA32F;
    sceneDesc.targets.colors[1] = prisma::TextureFormat::RGBA16F;
    sceneDesc.targets.colors[2] = prisma::TextureFormat::RGBA16F;
    sceneDesc.targets.depth = prisma::TextureFormat::Depth32F;
    sceneDesc.debugName = "reflective shadow map";
    const prisma::PipelineHandle mapPipeline = driver->createPipeline(sceneDesc);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.shader = vplShader;
    computeDesc.uniformBlockCount = 1;
    computeDesc.uniformBlocks[0].name = "Params";
    computeDesc.uniformBlocks[0].slot = 0;
    computeDesc.textureCount = 3;
    computeDesc.textures[0].name = "uPosition";
    computeDesc.textures[0].slot = 0;
    computeDesc.textures[1].name = "uNormal";
    computeDesc.textures[1].slot = 1;
    computeDesc.textures[2].name = "uAlbedo";
    computeDesc.textures[2].slot = 2;
    computeDesc.storageBufferCount = 1;
    computeDesc.storageBuffers[0].name = "Vpls";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.debugName = "virtual point lights";
    const prisma::PipelineHandle vplPipeline = driver->createComputePipeline(computeDesc);

    driver->destroy(sceneVertex);
    driver->destroy(sceneFragment);
    driver->destroy(rsmFragment);
    driver->destroy(vplShader);

    ready = ready && white.valid() && sampler.valid() && mapSampler.valid() &&
            shadowSampler.valid() && geometryBuffer.valid() && mapPosition.valid() &&
            mapNormal.valid() && mapAlbedo.valid() && mapDepth.valid() && uniformBuffer.valid() &&
            markerBuffer.valid() && paramsBuffer.valid() && directBuffer.valid() &&
            clusteredBuffer.valid() && vplBuffer.valid() && scenePipeline.valid() &&
            mapPipeline.valid() && vplPipeline.valid();
    if (!ready) log_error("instant radiosity: resource creation failed");

    prisma::RenderPassDesc mapPass;
    mapPass.colors[0].texture = mapPosition;
    mapPass.colors[1].texture = mapNormal;
    mapPass.colors[2].texture = mapAlbedo;
    mapPass.colorCount = 3;
    mapPass.depth.texture = mapDepth;
    mapPass.clearColor[0] = mapPass.clearColor[1] = mapPass.clearColor[2] = 0.0f;
    mapPass.clearColor[3] = 0.0f;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;

    ct::Vector<Vpl> vpls;
    vpls.resize(kMaxVpls);
    bool reported = false;
    float radiusScale = 1.0f;
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_I)) indirectOn = !indirectOn;
        if (key_pressed(window, KEY_V)) showMarkers = !showMarkers;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = still ? kStillTime : static_cast<float>(time_seconds()) + kStillTime;

        // the camera outside the open side of the room
        const float orbit = 0.12f * sinf(time * 0.3f);
        const Math::Vec3 eye(6.4f * sinf(orbit), 2.1f, 6.4f * cosf(orbit));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.85f, aspect, 0.1f, 40.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, Math::Vec3(0.0f, 1.9f, 0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f));

        // the light, a spot under the ceiling that swings over the room
        const Math::Vec3 lightPosition(0.9f * sinf(time * 0.6f), 3.8f, 0.5f * cosf(time * 0.45f));
        const Math::Vec3 lightTarget(0.1f * sinf(time * 0.5f), 0.4f, 0.2f);
        Math::Vec3 lightDirection = lightTarget - lightPosition;
        lightDirection = lightDirection * (1.0f / lightDirection.Length());
        const float tangent = tanf(kSpotOuter);
        const Math::Mat4 lightView = Math::Mat4::LookAt(lightPosition, lightTarget,
                Math::Vec3(0.0f, 0.0f, -1.0f));
        const Math::Mat4 lightProjection =
                zenapp::perspectiveZeroToOne(2.0f * atanf(tangent), 1.0f, 0.05f, 14.0f);
        const Math::Mat4 lightViewProjection = lightProjection * lightView;

        zenapp::LightSet spotSet;
        zenapp::clearLights(&spotSet);
        const float spotColor[3] = { 1.0f, 0.95f, 0.85f };
        const float position[3] = { lightPosition.x, lightPosition.y, lightPosition.z };
        const float direction[3] = { lightDirection.x, lightDirection.y, lightDirection.z };
        zenapp::addSpotLight(&spotSet, position, direction, spotColor, lightIntensity, 14.0f,
                kSpotInner, kSpotOuter);

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = exposure;
        frame.exposure[1] = frame.exposure[2] = frame.exposure[3] = 0.0f;
        memcpy(uniforms.data(), &frame, sizeof(frame));
        frame.viewProjection = lightViewProjection;
        frame.camera[0] = lightPosition.x;
        frame.camera[1] = lightPosition.y;
        frame.camera[2] = lightPosition.z;
        memcpy(uniforms.data() + frameStride, &frame, sizeof(frame));

        for (unsigned part = 0; part < kPartCount; ++part)
        {
            ObjectUniforms object;
            memset(&object, 0, sizeof(object));
            object.model = part == kPartDwarf ? dwarfModel : Math::Mat4::Identity();
            memcpy(object.tint, partTint[part], sizeof(object.tint));
            object.material[0] = partRoughness[part];
            memcpy(uniforms.data() + objectBase + static_cast<size_t>(part) * objectStride, &object,
                    sizeof(object));
        }

        VplParams params;
        params.params[0] = static_cast<float>(vplCount);
        params.params[1] = tangent;
        params.params[2] = lightIntensity;
        params.params[3] = spotSet.lights[0].spot[0];
        for (int c = 0; c < 3; ++c) params.color[c] = spotColor[c];
        params.color[3] = spotSet.lights[0].spot[1];

        DirectUniforms direct;
        direct.light = spotSet.lights[0];
        direct.lightViewProjection = lightViewProjection;
        direct.shadow[0] = 0.0012f;
        direct.shadow[1] = 0.06f;
        direct.shadow[2] = indirectOn ? indirectScale : 0.0f;
        direct.shadow[3] = directScale;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(),
                static_cast<std::uint32_t>(uniforms.size()));
        driver->updateBuffer(paramsBuffer, 0, &params, sizeof(params));
        driver->updateBuffer(directBuffer, 0, &direct, sizeof(direct));

        // what the light sees
        driver->beginRenderPass(mapPass);
        driver->bindPipeline(mapPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, frameStride, sizeof(FrameUniforms));
        driver->bindVertexBuffer(0, geometryBuffer, 0);
        for (unsigned part = 0; part < 4; ++part)
        {
            driver->bindUniformBuffer(2, uniformBuffer, objectBase + part * objectStride,
                    sizeof(ObjectUniforms));
            driver->bindTexture(3, white, sampler);
            driver->draw(partCount[part], partFirst[part]);
        }
        driver->bindUniformBuffer(2, uniformBuffer, objectBase + kPartDwarf * objectStride,
                sizeof(ObjectUniforms));
        for (unsigned i = 0; i < dwarf.subsetCount(0); ++i)
        {
            prisma::TextureHandle diffuse = dwarf.diffuse(dwarf.subset(0, i).materialId);
            driver->bindTexture(3, diffuse.valid() ? diffuse : white, sampler);
            dwarf.drawSubset(driver, 0, i);
        }
        driver->endRenderPass();

        // the virtual point lights sampled from it
        driver->beginComputePass();
        driver->bindPipeline(vplPipeline);
        driver->bindUniformBuffer(0, paramsBuffer, 0, sizeof(VplParams));
        driver->bindTexture(0, mapPosition, mapSampler);
        driver->bindTexture(1, mapNormal, mapSampler);
        driver->bindTexture(2, mapAlbedo, mapSampler);
        driver->bindStorageBuffer(0, vplBuffer, 0, sizeof(Vpl) * kMaxVpls);
        driver->dispatch((vplCount + kGroupSize - 1) / kGroupSize, 1, 1);
        driver->endComputePass();

        // and into the froxels as lights that shine about their normal
        zenapp::LightSet lights;
        zenapp::clearLights(&lights);
        unsigned valid = 0;
        if (driver->readBuffer(vplBuffer, 0, sizeof(Vpl) * vplCount, vpls.data()))
        {
            for (unsigned i = 0; i < vplCount; ++i)
            {
                const Vpl& vpl = vpls[i];
                if (vpl.position[3] < 0.5f) continue;
                ++valid;
                // a clamped cosine lobe is a cosine squared lobe of 3/2 the peak intensity
                float color[3];
                for (int c = 0; c < 3; ++c) color[c] = vpl.flux[c] * (3.0f / (2.0f * 3.14159265f));
                const float peak = maxChannel(color);
                float radius = sqrtf(peak / cullThreshold);
                radius = radius < 0.6f ? 0.6f : (radius > 4.0f ? 4.0f : radius);
                const float lifted[3] = { vpl.position[0] + vpl.normal[0] * 0.04f,
                    vpl.position[1] + vpl.normal[1] * 0.04f, vpl.position[2] + vpl.normal[2] * 0.04f };
                zenapp::addSpotLight(&lights, lifted, vpl.normal, color, 1.0f, radius, 0.0f,
                        1.57079633f);
            }
        }
        froxelizer.prepare(static_cast<unsigned>(width), static_cast<unsigned>(height),
                projection.Data(), 0.1f, 40.0f);
        // The froxels keep a fixed number of light records. When the ranges of the virtual lights
        // need more, all ranges shrink until the lists fit; they grow back while there is room.
        if (frames % 30 == 0 && radiusScale < 1.0f)
            radiusScale = radiusScale * 1.06f > 1.0f ? 1.0f : radiusScale * 1.06f;
        float baseRadius[kMaxVpls];
        for (unsigned i = 0; i < lights.count; ++i) baseRadius[i] = lights.culling[i].radius;
        unsigned records = 0;
        for (int attempt = 0; attempt < 24; ++attempt)
        {
            for (unsigned i = 0; i < lights.count; ++i)
            {
                const float radius = baseRadius[i] * radiusScale;
                lights.culling[i].radius = radius;
                lights.lights[i].positionFalloff[3] = 1.0f / (radius * radius);
            }
            zenapp::buildClusteredBuffers(lights, froxelizer, view.Data(), &clustered);
            records = 0;
            for (unsigned i = 0; i < froxelizer.froxelCount(); ++i)
                records += froxelizer.entries()[i] & 0xFFu;
            if (!froxelizer.recordsOverflowed()) break;
            radiusScale *= 0.93f;
        }
        const float scaleUsed = radiusScale;
        driver->updateBuffer(clusteredBuffer, 0, &clustered, sizeof(clustered));

        if (!reported)
        {
            log_info("instant radiosity: %u of %u virtual lights valid, %u froxels with %u light "
                      "entries, the ranges of the lights at %.0f%% to fit the %u records%s",
                    valid, vplCount, froxelizer.froxelCount(), records, scaleUsed * 100.0f,
                    static_cast<unsigned>(zenapp::Froxelizer::kRecordCount),
                    froxelizer.recordsOverflowed() ? " (overflow)" : "");
            reported = true;
        }

        unsigned markers = 0;
        if (showMarkers)
        {
            for (unsigned i = 0; i < vplCount; ++i)
            {
                const Vpl& vpl = vpls[i];
                if (vpl.position[3] < 0.5f) continue;
                ObjectUniforms object;
                memset(&object, 0, sizeof(object));
                const Math::Vec3 at(vpl.position[0] + vpl.normal[0] * 0.04f,
                        vpl.position[1] + vpl.normal[1] * 0.04f,
                        vpl.position[2] + vpl.normal[2] * 0.04f);
                object.model = Math::Mat4::Translation(at) *
                               Math::Mat4::Scale(Math::Vec3(ballScale, ballScale, ballScale)) *
                               Math::Mat4::Translation(-ballCenter);
                const float peak = maxChannel(vpl.flux);
                const float norm = peak > 1e-6f ? 1.0f / peak : 0.0f;
                for (int c = 0; c < 3; ++c) object.tint[c] = 0.25f + 0.75f * vpl.flux[c] * norm;
                object.tint[3] = 1.0f;
                object.material[2] = 1.0f;
                memcpy(markerBytes.data() + static_cast<size_t>(markers) * objectStride, &object,
                        sizeof(object));
                ++markers;
            }
            if (markers > 0)
                driver->updateBuffer(markerBuffer, 0, markerBytes.data(),
                        markers * objectStride);
        }

        driver->beginRenderPass(pass);
        driver->bindPipeline(scenePipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        zenapp::bindIbl(driver, ibl);
        driver->bindUniformBuffer(3, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, cluster),
                sizeof(zenapp::ClusterUniforms));
        driver->bindUniformBuffer(4, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, lights),
                sizeof(clustered.lights));
        driver->bindUniformBuffer(5, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, froxels),
                sizeof(clustered.froxels));
        driver->bindUniformBuffer(6, clusteredBuffer, offsetof(zenapp::ClusteredBuffers, records),
                sizeof(clustered.records));
        driver->bindUniformBuffer(7, directBuffer, 0, sizeof(DirectUniforms));
        driver->bindTexture(4, mapDepth, shadowSampler);

        driver->bindVertexBuffer(0, geometryBuffer, 0);
        for (unsigned part = 0; part < 4; ++part)
        {
            driver->bindUniformBuffer(2, uniformBuffer, objectBase + part * objectStride,
                    sizeof(ObjectUniforms));
            driver->bindTexture(3, white, sampler);
            driver->draw(partCount[part], partFirst[part]);
        }
        driver->bindUniformBuffer(2, uniformBuffer, objectBase + kPartDwarf * objectStride,
                sizeof(ObjectUniforms));
        for (unsigned i = 0; i < dwarf.subsetCount(0); ++i)
        {
            prisma::TextureHandle diffuse = dwarf.diffuse(dwarf.subset(0, i).materialId);
            driver->bindTexture(3, diffuse.valid() ? diffuse : white, sampler);
            dwarf.drawSubset(driver, 0, i);
        }
        driver->bindTexture(3, white, sampler);
        for (unsigned i = 0; i < markers; ++i)
        {
            driver->bindUniformBuffer(2, markerBuffer, i * objectStride, sizeof(ObjectUniforms));
            for (unsigned s = 0; s < ball.subsetCount(0); ++s) ball.drawSubset(driver, 0, s);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(vplPipeline);
    driver->destroy(mapPipeline);
    driver->destroy(scenePipeline);
    driver->destroy(vplBuffer);
    driver->destroy(clusteredBuffer);
    driver->destroy(directBuffer);
    driver->destroy(paramsBuffer);
    driver->destroy(markerBuffer);
    driver->destroy(uniformBuffer);
    driver->destroy(mapDepth);
    driver->destroy(mapAlbedo);
    driver->destroy(mapNormal);
    driver->destroy(mapPosition);
    driver->destroy(geometryBuffer);
    driver->destroy(shadowSampler);
    driver->destroy(mapSampler);
    driver->destroy(sampler);
    driver->destroy(white);
    ball.destroy(driver);
    dwarf.destroy(driver);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
