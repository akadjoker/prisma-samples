#include "common/AreaLights.h"
#include "common/Ibl.h"
#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "light.frag.h"
#include "scene.frag.h"
#include "scene.vert.h"

namespace
{

const float kDwarfHeight = 2.0f;
const float kRoomHalf = 4.0f;
const float kRoomHeight = 4.2f;
const float kStillTime = 1.0f;
const unsigned kLightCount = 4;
const unsigned kObjectCount = 9;

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

struct AreaUniforms
{
    float count[4];
    zenapp::AreaLightData lights[kLightCount];
};

static_assert(sizeof(zenapp::AreaLightData) == 96, "AreaLight of ltc.glsl");

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

void addQuad(SceneVertex* out, const Math::Vec3& origin, const Math::Vec3& across,
        const Math::Vec3& up, const Math::Vec3& normal, float tile)
{
    const float u[6] = { 0, 1, 1, 0, 1, 0 };
    const float v[6] = { 0, 0, 1, 0, 1, 1 };
    const float acrossLength = across.Length();
    const float upLength = up.Length();
    for (int i = 0; i < 6; ++i)
    {
        const Math::Vec3 p = origin + across * u[i] + up * v[i];
        out[i].position[0] = p.x;
        out[i].position[1] = p.y;
        out[i].position[2] = p.z;
        out[i].normal[0] = normal.x;
        out[i].normal[1] = normal.y;
        out[i].normal[2] = normal.z;
        out[i].uv[0] = u[i] * acrossLength / tile;
        out[i].uv[1] = (1.0f - v[i]) * upLength / tile;
    }
}

// The two triangles of a rectangle light, wound the way it shines.
void lightQuad(SceneVertex* out, const zenapp::AreaLightData& light)
{
    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    const float u[4] = { 0, 1, 1, 0 };
    const float v[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 6; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            out[i].position[c] = light.corners[order[i]][c];
            out[i].normal[c] = 0.0f;
        }
        out[i].uv[0] = u[order[i]];
        out[i].uv[1] = v[order[i]];
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

// The four lights of the room: a warm panel above the dwarf that turns slowly, an orange one on
// the left wall, a blue one on the right wall and a thin cold strip along the back wall.
void roomLights(float time, float intensity, bool twoSided, const bool* on,
        zenapp::AreaLightData* lights)
{
    const float angle = time * 0.45f;
    const float c = cosf(angle);
    const float s = sinf(angle);
    {
        const float center[3] = { 0.0f, 3.2f, 0.6f };
        const float right[3] = { 1.5f * c, 0.0f, 1.5f * s };
        const float up[3] = { -0.7f * s, 0.0f, 0.7f * c };
        const float color[3] = { 1.0f, 0.86f, 0.68f };
        lights[0] = zenapp::makeRectangleLight(center, right, up, color,
                on[0] ? 9.0f * intensity : 0.0f, twoSided);
    }
    {
        const float center[3] = { -3.92f, 1.3f, -0.4f };
        const float right[3] = { 0.0f, 0.0f, -1.1f };
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        const float color[3] = { 1.0f, 0.32f, 0.08f };
        lights[1] = zenapp::makeRectangleLight(center, right, up, color,
                on[1] ? 7.0f * intensity : 0.0f, twoSided);
    }
    {
        const float center[3] = { 3.92f, 1.3f, -0.4f };
        const float right[3] = { 0.0f, 0.0f, 1.1f };
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        const float color[3] = { 0.15f, 0.4f, 1.0f };
        lights[2] = zenapp::makeRectangleLight(center, right, up, color,
                on[2] ? 7.0f * intensity : 0.0f, twoSided);
    }
    {
        const float center[3] = { 0.0f, 2.5f, -3.92f };
        const float right[3] = { 2.4f, 0.0f, 0.0f };
        const float up[3] = { 0.0f, 0.1f, 0.0f };
        const float color[3] = { 0.75f, 0.9f, 1.0f };
        lights[3] = zenapp::makeRectangleLight(center, right, up, color,
                on[3] ? 9.0f * intensity : 0.0f, twoSided);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const bool twoSided = zenapp::hasArgument(argc, argv, "twosided");
    const float intensity = argumentFloat(argc, argv, "intensity", 1.0f);
    const float exposure = argumentFloat(argc, argv, "exposure", 1.0f);
    const float ambient = argumentFloat(argc, argv, "ambient", 0.03f);
    float floorRoughness = argumentFloat(argc, argv, "floor", 0.22f);
    const float dwarfRoughness = argumentFloat(argc, argv, "dwarf", 0.5f);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 37 area lights", driverType);
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
#ifdef __EMSCRIPTEN__
    // a smaller probe to download; it only gives the dim ambient light of the room
    zenapp::mediaPath("Light Probes/grace_cross.dds", path, sizeof(path));
#else
    zenapp::mediaPath("Light Probes/uffizi_cross.dds", path, sizeof(path));
#endif
    zenapp::EnvironmentFaces faces;
    bool ready = zenapp::loadEnvironmentFaces(path, &faces);
    if (!ready) log_error("area lights: cannot read %s", path);
    zenapp::Ibl ibl;
    if (ready && !zenapp::createIbl(driver, faces, &ibl, ambient))
    {
        log_error("area lights: cannot build the image based lighting resources");
        ready = false;
    }

    zenapp::mediaPath("Dwarf/dwarf.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh dwarf;
    const bool dwarfLoaded = dwarf.load(driver, path) && dwarf.meshCount() == 1 && sceneLayout(dwarf);
    if (!dwarfLoaded) log_error("dwarf: cannot load %s", path);
    ready = ready && dwarfLoaded;

    Math::Vec3 low(0.0f, 0.0f, 0.0f);
    Math::Vec3 high(1.0f, 1.0f, 1.0f);
    if (dwarfLoaded) bounds(dwarf, &low, &high);
    const float extent = high.y - low.y;
    const float scale = extent > 0.0f ? kDwarfHeight / extent : 1.0f;
    const Math::Vec3 center = (low + high) * 0.5f;
    const Math::Mat4 dwarfModel = Math::Mat4::RotationY(argumentFloat(argc, argv, "turn", 3.14159265f)) *
                                  Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                                  Math::Mat4::Translation(Math::Vec3(-center.x, -low.y, -center.z));

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc textureDesc;
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.data = whitePixel;
    textureDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(textureDesc);

    zenapp::mediaPath("misc/cellfloor.dds", path, sizeof(path));
    const prisma::TextureHandle floorTexture = zenapp::loadTexture(driver, path, true, true);
    zenapp::mediaPath("misc/cellwall.dds", path, sizeof(path));
    const prisma::TextureHandle wallTexture = zenapp::loadTexture(driver, path, true, true);
    if (!floorTexture.valid()) log_error("room: cellfloor.dds not loaded, using a plain tint");
    if (!wallTexture.valid()) log_error("room: cellwall.dds not loaded, using a plain tint");

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "albedo sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    const prisma::TextureHandle ltcTable = zenapp::createLtcTexture(driver);
    const prisma::SamplerHandle ltcSampler = zenapp::createLtcSampler(driver);

    SceneVertex room[24];
    const float h = kRoomHalf;
    const float t = kRoomHeight;
    addQuad(room, Math::Vec3(-h, 0, h), Math::Vec3(2 * h, 0, 0), Math::Vec3(0, 0, -2 * h),
            Math::Vec3(0, 1, 0), 2.0f);
    addQuad(room + 6, Math::Vec3(-h, 0, -h), Math::Vec3(2 * h, 0, 0), Math::Vec3(0, t, 0),
            Math::Vec3(0, 0, 1), 2.5f);
    addQuad(room + 12, Math::Vec3(-h, 0, h), Math::Vec3(0, 0, -2 * h), Math::Vec3(0, t, 0),
            Math::Vec3(1, 0, 0), 2.5f);
    addQuad(room + 18, Math::Vec3(h, 0, -h), Math::Vec3(0, 0, 2 * h), Math::Vec3(0, t, 0),
            Math::Vec3(-1, 0, 0), 2.5f);

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(room);
    bufferDesc.data = room;
    bufferDesc.debugName = "room vertices";
    const prisma::BufferHandle roomBuffer = driver->createBuffer(bufferDesc);

    SceneVertex quads[kLightCount * 6];
    bufferDesc.size = sizeof(quads);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "light quads";
    const prisma::BufferHandle quadBuffer = driver->createBuffer(bufferDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const std::uint32_t frameStride = alignUp(sizeof(FrameUniforms), alignment);
    const std::uint32_t objectStride = alignUp(sizeof(ObjectUniforms), alignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(static_cast<size_t>(frameStride) + objectStride * kObjectCount);
    memset(uniforms.data(), 0, uniforms.size());
    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(uniforms.size());
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);
    bufferDesc.size = sizeof(AreaUniforms);
    bufferDesc.debugName = "area lights";
    const prisma::BufferHandle areaBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle sceneVertex = zenapp::createShader(driver, scene_vert);
    const prisma::ShaderHandle sceneFragment = zenapp::createShader(driver, scene_frag);
    const prisma::ShaderHandle lightFragment = zenapp::createShader(driver, light_frag);

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

    sceneDesc.fragmentShader = lightFragment;
    sceneDesc.cullMode = prisma::CullMode::None;
    sceneDesc.debugName = "light quads";
    const prisma::PipelineHandle lightPipeline = driver->createPipeline(sceneDesc);

    driver->destroy(sceneVertex);
    driver->destroy(sceneFragment);
    driver->destroy(lightFragment);

    ready = ready && white.valid() && sampler.valid() && ltcTable.valid() && ltcSampler.valid() &&
            roomBuffer.valid() && quadBuffer.valid() && uniformBuffer.valid() &&
            areaBuffer.valid() && scenePipeline.valid() && lightPipeline.valid();
    if (!ready) log_error("area lights: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;

    bool on[kLightCount] = { true, true, true, true };
    double previous = time_seconds();
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        for (unsigned i = 0; i < kLightCount; ++i)
            if (key_pressed(window, KEY_ONE + i)) on[i] = !on[i];
        const double now = time_seconds();
        const float dt = static_cast<float>(now - previous);
        previous = now;
        if (key_down(window, KEY_UP)) floorRoughness = fminf(floorRoughness * expf(dt), 1.0f);
        if (key_down(window, KEY_DOWN)) floorRoughness = fmaxf(floorRoughness * expf(-dt), 0.05f);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = still ? kStillTime : static_cast<float>(now) + kStillTime;

        const float orbit = 0.25f * sinf(time * 0.3f);
        const Math::Vec3 eye(6.6f * sinf(orbit), 2.5f, 6.6f * cosf(orbit));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.9f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, Math::Vec3(0.0f, 1.1f, 0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = exposure;
        frame.exposure[1] = 0.0f;
        frame.exposure[2] = 1.0f;
        frame.exposure[3] = 0.0f;
        memcpy(uniforms.data(), &frame, sizeof(frame));

        AreaUniforms area;
        memset(&area, 0, sizeof(area));
        area.count[0] = static_cast<float>(kLightCount);
        roomLights(time, intensity, twoSided, on, area.lights);
        for (unsigned i = 0; i < kLightCount; ++i) lightQuad(quads + i * 6, area.lights[i]);

        // floor, back wall, left wall, right wall, dwarf, then the four light quads
        const float roughness[5] = { floorRoughness, 0.75f, 0.75f, 0.75f, dwarfRoughness };
        const Math::Mat4 identity = Math::Mat4::Identity();
        for (unsigned i = 0; i < kObjectCount; ++i)
        {
            ObjectUniforms object;
            memset(&object, 0, sizeof(object));
            object.model = i == 4 ? dwarfModel : identity;
            if (i < 5)
            {
                object.tint[0] = object.tint[1] = object.tint[2] = i == 0 ? 0.9f : 1.0f;
                object.tint[3] = 1.0f;
                object.material[0] = roughness[i];
                object.material[1] = 0.0f;
            }
            else
            {
                const zenapp::AreaLightData& light = area.lights[i - 5];
                for (int c = 0; c < 3; ++c) object.tint[c] = light.colorIntensity[c];
                object.tint[3] = 1.0f;
                object.material[2] = light.colorIntensity[3] > 0.0f ? 1.1f : 0.02f;
            }
            memcpy(uniforms.data() + frameStride + static_cast<size_t>(i) * objectStride, &object,
                    sizeof(object));
        }

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(),
                static_cast<std::uint32_t>(uniforms.size()));
        driver->updateBuffer(areaBuffer, 0, &area, sizeof(area));
        driver->updateBuffer(quadBuffer, 0, quads, sizeof(quads));
        driver->beginRenderPass(pass);

        driver->bindPipeline(scenePipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindUniformBuffer(3, areaBuffer, 0, sizeof(AreaUniforms));
        zenapp::bindIbl(driver, ibl);
        driver->bindTexture(2, ltcTable, ltcSampler);

        driver->bindVertexBuffer(0, roomBuffer, 0);
        for (unsigned s = 0; s < 4; ++s)
        {
            const prisma::TextureHandle albedo =
                    s == 0 ? (floorTexture.valid() ? floorTexture : white)
                           : (wallTexture.valid() ? wallTexture : white);
            driver->bindUniformBuffer(2, uniformBuffer, frameStride + s * objectStride,
                    sizeof(ObjectUniforms));
            driver->bindTexture(3, albedo, sampler);
            driver->draw(6, s * 6);
        }
        driver->bindUniformBuffer(2, uniformBuffer, frameStride + 4 * objectStride,
                sizeof(ObjectUniforms));
        for (unsigned i = 0; i < dwarf.subsetCount(0); ++i)
        {
            prisma::TextureHandle diffuse = dwarf.diffuse(dwarf.subset(0, i).materialId);
            driver->bindTexture(3, diffuse.valid() ? diffuse : white, sampler);
            dwarf.drawSubset(driver, 0, i);
        }

        driver->bindPipeline(lightPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindVertexBuffer(0, quadBuffer, 0);
        for (unsigned i = 0; i < kLightCount; ++i)
        {
            driver->bindUniformBuffer(2, uniformBuffer, frameStride + (5 + i) * objectStride,
                    sizeof(ObjectUniforms));
            driver->draw(6, i * 6);
        }

        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(lightPipeline);
    driver->destroy(scenePipeline);
    driver->destroy(areaBuffer);
    driver->destroy(uniformBuffer);
    driver->destroy(quadBuffer);
    driver->destroy(roomBuffer);
    driver->destroy(ltcSampler);
    driver->destroy(ltcTable);
    driver->destroy(sampler);
    if (floorTexture.valid()) driver->destroy(floorTexture);
    if (wallTexture.valid()) driver->destroy(wallTexture);
    driver->destroy(white);
    dwarf.destroy(driver);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
