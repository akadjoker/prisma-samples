#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/hashmap.hpp>
#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ambient.frag.h"
#include "ball.frag.h"
#include "lit.frag.h"
#include "overlay.frag.h"
#include "scene.vert.h"
#include "volume.frag.h"
#include "volume.vert.h"

namespace
{

const float kNear = 0.1f;
const float kAmbient = 0.10f;
const float kLightColor = 5.5f;
const float kLightFalloff = 1.2f;
const float kDwarfHeight = 2.0f;
const float kBallRadius = 0.12f;
const float kRoomHalf = 4.0f;
const float kRoomHeight = 4.2f;
const float kOrbitRadius = 2.55f;
const float kStillTime = 1.31f;
const std::uint32_t kNoFace = 0xFFFFFFFFu;

struct Block
{
    Math::Mat4 mvp;
    Math::Mat4 mv;
    Math::Mat4 proj;
    float lightView[4];
    float params[4];
};

static_assert(sizeof(Block) == 224, "uniform block size");

struct SceneVertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

struct VolumeVertex
{
    float position[3];
    float normal[3];
};

struct SilhouetteEdge
{
    std::uint32_t faceA;
    std::uint32_t faceB;
};

struct ShadowVolume
{
    ct::Vector<VolumeVertex> vertices;
    ct::Vector<std::uint32_t> indices;
    ct::Vector<SilhouetteEdge> edges;
    ct::Vector<std::uint32_t> faceBases;
    ct::Vector<Math::Vec3> faceNormals;
    ct::Vector<Math::Vec3> facePoints;
    unsigned corners = 0;
    unsigned welded = 0;
    unsigned faces = 0;
    unsigned degenerate = 0;
    unsigned shared = 0;
    unsigned open = 0;
    unsigned loops = 0;
    unsigned patched = 0;
    unsigned unresolved = 0;
    unsigned agreeing = 0;
};

std::uint64_t edgeKey(std::uint32_t from, std::uint32_t to)
{
    return (static_cast<std::uint64_t>(from) << 32) | to;
}

std::uint32_t quantize(float value, float low, float inverseExtent)
{
    float t = (value - low) * inverseExtent * 1048575.0f + 0.5f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1048575.0f) t = 1048575.0f;
    return static_cast<std::uint32_t>(t);
}

bool gatherCorners(const zenapp::SdkMesh& mesh, ct::Vector<Math::Vec3>* positions,
        ct::Vector<Math::Vec3>* normals)
{
    const zenapp::SdkMeshData& data = mesh.data();
    for (unsigned m = 0; m < mesh.meshCount(); ++m)
    {
        const zenapp::SdkMeshHeader& part = data.meshes[m];
        const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[part.vertexBuffers[0]];
        const zenapp::SdkIndexBuffer& indexBuffer = data.indexBuffers[part.indexBuffer];
        unsigned positionOffset = 0;
        unsigned normalOffset = 0;
        for (unsigned e = 0; e < 32 && buffer.decl[e].stream != 0xFF; ++e)
        {
            if (buffer.decl[e].usage == 0) positionOffset = buffer.decl[e].offset;
            if (buffer.decl[e].usage == 3) normalOffset = buffer.decl[e].offset;
        }
        const unsigned char* indexData = data.file.data() + indexBuffer.dataOffset;
        for (unsigned s = 0; s < mesh.subsetCount(m); ++s)
        {
            const zenapp::SdkSubset& subset = mesh.subset(m, s);
            if (subset.primitiveType != 0 || subset.indexCount % 3 != 0) return false;
            for (uint64_t i = 0; i < subset.indexCount; ++i)
            {
                const uint64_t at = subset.indexStart + i;
                uint64_t index;
                if (indexBuffer.indexType == 1)
                {
                    uint32_t value;
                    memcpy(&value, indexData + at * 4, 4);
                    index = value;
                }
                else
                {
                    uint16_t value;
                    memcpy(&value, indexData + at * 2, 2);
                    index = value;
                }
                const uint64_t vertex = subset.vertexStart + index;
                if (vertex >= buffer.numVertices) return false;
                const unsigned char* base =
                        data.file.data() + buffer.dataOffset + vertex * buffer.strideBytes;
                float position[3];
                float normal[3];
                memcpy(position, base + positionOffset, sizeof(position));
                memcpy(normal, base + normalOffset, sizeof(normal));
                positions->push_back(Math::Vec3(position[0], position[1], position[2]));
                normals->push_back(Math::Vec3(normal[0], normal[1], normal[2]));
            }
        }
    }
    return positions->size() > 0;
}

std::uint32_t addFace(ShadowVolume* volume, const Math::Vec3& p0, const Math::Vec3& p1,
        const Math::Vec3& p2)
{
    const Math::Vec3 cross = Math::Vec3::Cross(p1 - p0, p2 - p1);
    const float length = cross.Length();
    const Math::Vec3 normal = length > 0.0f ? cross / length : Math::Vec3(0.0f, 1.0f, 0.0f);
    const std::uint32_t base = static_cast<std::uint32_t>(volume->vertices.size());
    const Math::Vec3* corners[3] = { &p0, &p1, &p2 };
    for (unsigned c = 0; c < 3; ++c)
    {
        VolumeVertex vertex;
        vertex.position[0] = corners[c]->x;
        vertex.position[1] = corners[c]->y;
        vertex.position[2] = corners[c]->z;
        vertex.normal[0] = normal.x;
        vertex.normal[1] = normal.y;
        vertex.normal[2] = normal.z;
        volume->vertices.push_back(vertex);
        volume->indices.push_back(base + c);
    }
    volume->faceBases.push_back(base);
    volume->faceNormals.push_back(normal);
    volume->facePoints.push_back(p0);
    return static_cast<std::uint32_t>(volume->faceBases.size() - 1);
}

void addSide(ShadowVolume* volume, std::uint32_t faceA, unsigned cornerA, std::uint32_t faceB,
        unsigned cornerB)
{
    const std::uint32_t a0 = volume->faceBases[faceA] + cornerA;
    const std::uint32_t a1 = volume->faceBases[faceA] + (cornerA + 1) % 3;
    const std::uint32_t b0 = volume->faceBases[faceB] + cornerB;
    const std::uint32_t b1 = volume->faceBases[faceB] + (cornerB + 1) % 3;
    volume->indices.push_back(a1);
    volume->indices.push_back(a0);
    volume->indices.push_back(b0);
    volume->indices.push_back(b1);
    volume->indices.push_back(b0);
    volume->indices.push_back(a0);
    SilhouetteEdge edge;
    edge.faceA = faceA;
    edge.faceB = faceB;
    volume->edges.push_back(edge);
}

struct OpenEdge
{
    std::uint32_t from;
    std::uint32_t to;
    std::uint32_t face;
    unsigned corner;
    Math::Vec3 start;
    Math::Vec3 end;
};

void patchHoles(ShadowVolume* volume, const ct::Vector<OpenEdge>& open)
{
    ct::HashMap<std::uint32_t, std::uint32_t> outgoing;
    for (size_t i = 0; i < open.size(); ++i)
        if (!outgoing.contains(open[i].from)) outgoing.put(open[i].from, static_cast<std::uint32_t>(i));

    ct::Vector<std::uint8_t> visited;
    visited.resize(open.size());
    for (size_t i = 0; i < open.size(); ++i) visited[i] = 0;

    for (size_t first = 0; first < open.size(); ++first)
    {
        if (visited[first]) continue;
        ct::Vector<std::uint32_t> loop;
        bool closed = false;
        std::uint32_t current = static_cast<std::uint32_t>(first);
        for (;;)
        {
            if (visited[current]) break;
            visited[current] = 1;
            loop.push_back(current);
            const std::uint32_t* next = outgoing.find(open[current].to);
            if (!next) break;
            current = *next;
            if (current == first)
            {
                closed = true;
                break;
            }
        }
        if (!closed)
        {
            volume->unresolved += static_cast<unsigned>(loop.size());
            continue;
        }

        Math::Vec3 center(0.0f, 0.0f, 0.0f);
        for (size_t j = 0; j < loop.size(); ++j) center += open[loop[j]].start;
        center = center / static_cast<float>(loop.size());

        ct::Vector<std::uint32_t> patches;
        for (size_t j = 0; j < loop.size(); ++j)
        {
            const OpenEdge& edge = open[loop[j]];
            const std::uint32_t patch = addFace(volume, edge.end, edge.start, center);
            patches.push_back(patch);
            addSide(volume, edge.face, edge.corner, patch, 0);
        }
        for (size_t j = 0; j < loop.size(); ++j)
            addSide(volume, patches[j], 2, patches[(j + 1) % loop.size()], 1);
        ++volume->loops;
        volume->patched += static_cast<unsigned>(loop.size());
    }
}

bool buildShadowVolume(const zenapp::SdkMesh& mesh, ShadowVolume* volume)
{
    ct::Vector<Math::Vec3> positions;
    ct::Vector<Math::Vec3> normals;
    if (!gatherCorners(mesh, &positions, &normals)) return false;
    const unsigned corners = static_cast<unsigned>(positions.size());
    volume->corners = corners;

    Math::Vec3 low = positions[0];
    Math::Vec3 high = positions[0];
    for (unsigned i = 1; i < corners; ++i)
    {
        low = Math::Vec3::Min(low, positions[i]);
        high = Math::Vec3::Max(high, positions[i]);
    }
    const Math::Vec3 size = high - low;
    float extent = size.x > size.y ? size.x : size.y;
    if (size.z > extent) extent = size.z;
    if (extent <= 0.0f) return false;
    const float inverseExtent = 1.0f / extent;

    ct::Vector<std::uint32_t> welded;
    welded.resize(corners);
    ct::HashMap<std::uint64_t, std::uint32_t> unique;
    for (unsigned i = 0; i < corners; ++i)
    {
        const std::uint64_t key =
                static_cast<std::uint64_t>(quantize(positions[i].x, low.x, inverseExtent)) |
                (static_cast<std::uint64_t>(quantize(positions[i].y, low.y, inverseExtent))
                        << 20) |
                (static_cast<std::uint64_t>(quantize(positions[i].z, low.z, inverseExtent))
                        << 40);
        const std::uint32_t* found = unique.find(key);
        if (found)
        {
            welded[i] = *found;
        }
        else
        {
            const std::uint32_t id = static_cast<std::uint32_t>(unique.size());
            unique.put(key, id);
            welded[i] = id;
        }
    }
    volume->welded = static_cast<unsigned>(unique.size());

    const unsigned faceCount = corners / 3;
    ct::Vector<std::uint32_t> faceOf;
    faceOf.resize(faceCount);
    for (unsigned f = 0; f < faceCount; ++f)
    {
        faceOf[f] = kNoFace;
        const Math::Vec3& p0 = positions[f * 3];
        const Math::Vec3& p1 = positions[f * 3 + 1];
        const Math::Vec3& p2 = positions[f * 3 + 2];
        if (welded[f * 3] == welded[f * 3 + 1] || welded[f * 3 + 1] == welded[f * 3 + 2] ||
                welded[f * 3 + 2] == welded[f * 3])
        {
            ++volume->degenerate;
            continue;
        }
        const Math::Vec3 cross = Math::Vec3::Cross(p1 - p0, p2 - p1);
        const float length = cross.Length();
        if (!(length > extent * extent * 1e-12f))
        {
            ++volume->degenerate;
            continue;
        }
        const Math::Vec3 average = normals[f * 3] + normals[f * 3 + 1] + normals[f * 3 + 2];
        if (cross.Dot(average) > 0.0f) ++volume->agreeing;
        faceOf[f] = addFace(volume, p0, p1, p2);
    }
    volume->faces = static_cast<unsigned>(volume->faceBases.size());

    ct::HashMap<std::uint64_t, std::uint32_t> directed;
    for (unsigned f = 0; f < faceCount; ++f)
    {
        if (faceOf[f] == kNoFace) continue;
        for (unsigned k = 0; k < 3; ++k)
        {
            const std::uint64_t key = edgeKey(welded[f * 3 + k], welded[f * 3 + (k + 1) % 3]);
            if (!directed.contains(key)) directed.put(key, f * 3 + k);
        }
    }

    ct::Vector<std::uint8_t> used;
    used.resize(faceCount * 3);
    for (unsigned i = 0; i < faceCount * 3; ++i) used[i] = 0;

    ct::Vector<OpenEdge> open;
    for (unsigned f = 0; f < faceCount; ++f)
    {
        if (faceOf[f] == kNoFace) continue;
        for (unsigned k = 0; k < 3; ++k)
        {
            const unsigned edge = f * 3 + k;
            if (used[edge]) continue;
            used[edge] = 1;
            const std::uint32_t from = welded[f * 3 + k];
            const std::uint32_t to = welded[f * 3 + (k + 1) % 3];
            const std::uint32_t* own = directed.find(edgeKey(from, to));
            const std::uint32_t* twin = directed.find(edgeKey(to, from));
            if (own && *own == edge && twin && !used[*twin])
            {
                const unsigned other = *twin / 3;
                used[*twin] = 1;
                addSide(volume, faceOf[f], k, faceOf[other], *twin % 3);
                ++volume->shared;
            }
            else
            {
                OpenEdge record;
                record.from = from;
                record.to = to;
                record.face = faceOf[f];
                record.corner = k;
                record.start = positions[f * 3 + k];
                record.end = positions[f * 3 + (k + 1) % 3];
                open.push_back(record);
            }
        }
    }
    volume->open = static_cast<unsigned>(open.size());
    patchHoles(volume, open);
    return volume->faces > 0 && volume->welded > 0;
}

unsigned countSilhouette(const ShadowVolume& volume, const Math::Vec3& light)
{
    unsigned count = 0;
    for (size_t i = 0; i < volume.edges.size(); ++i)
    {
        const SilhouetteEdge& edge = volume.edges[i];
        const bool frontA =
                volume.faceNormals[edge.faceA].Dot(light - volume.facePoints[edge.faceA]) > 0.0f;
        const bool frontB =
                volume.faceNormals[edge.faceB].Dot(light - volume.facePoints[edge.faceB]) > 0.0f;
        if (frontA != frontB) ++count;
    }
    return count;
}

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

Math::Vec3 lightPosition(float time)
{
    const float angle = time * 0.6f;
    return Math::Vec3(cosf(angle) * kOrbitRadius, 1.0f + 0.3f * sinf(time * 0.7f),
            sinf(angle) * kOrbitRadius);
}

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void fillBlock(Block* block, const Math::Mat4& projection, const Math::Mat4& view,
        const Math::Mat4& model, const Math::Vec3& lightWorld)
{
    block->mv = view * model;
    block->mvp = projection * block->mv;
    block->proj = projection;
    const Math::Vec4 light = view * Math::Vec4(lightWorld.x, lightWorld.y, lightWorld.z, 1.0f);
    block->lightView[0] = light.x;
    block->lightView[1] = light.y;
    block->lightView[2] = light.z;
    block->lightView[3] = 1.0f;
    block->params[0] = kAmbient;
    block->params[1] = kLightColor * kLightFalloff * kLightFalloff;
    block->params[2] = 0.0f;
    block->params[3] = 0.0f;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    bool showVolume = zenapp::hasArgument(argc, argv, "volume");

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 28 shadow volume", driverType);
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
    zenapp::mediaPath("Dwarf/dwarf.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh dwarf;
    bool ready = dwarf.load(driver, path) && dwarf.meshCount() == 1;
    if (!ready) log_error("dwarf: cannot load %s", path);
    if (ready && !sceneLayout(dwarf))
    {
        log_error("dwarf: unexpected vertex layout");
        ready = false;
    }

    zenapp::mediaPath("misc/ball.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh ball;
    const bool ballLoaded = ball.load(driver, path, false) && ball.meshCount() == 1 && sceneLayout(ball);
    if (!ballLoaded) log_error("ball: cannot load %s", path);
    ready = ready && ballLoaded;

    ShadowVolume volume;
    if (ready && !buildShadowVolume(dwarf, &volume))
    {
        log_error("shadow volume: the welded mesh is empty");
        ready = false;
    }

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
    const Math::Mat4 dwarfModel = Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                                  Math::Mat4::Translation(Math::Vec3(-center.x, -low.y, -center.z));
    const Math::Mat4 dwarfInverse = dwarfModel.Inverse();
    const Math::Vec3 ballCenter = (ballLow + ballHigh) * 0.5f;
    const Math::Vec3 ballSize = (ballHigh - ballLow) * 0.5f;
    const float ballExtent = ballSize.x > ballSize.y ? ballSize.x : ballSize.y;
    const float ballScale = ballExtent > 0.0f ? kBallRadius / ballExtent : 1.0f;

    if (ready)
    {
        const Math::Vec3 light0 = lightPosition(kStillTime);
        log_info("shadow volume: dwarf bounds (%.2f %.2f %.2f) to (%.2f %.2f %.2f)", low.x, low.y,
                low.z, high.x, high.y, high.z);
        log_info("shadow volume: %u corners, %u welded vertices, %u faces (%u degenerate), "
                  "face normals agree with vertex normals on %u of %u",
                volume.corners, volume.welded, volume.faces, volume.degenerate, volume.agreeing,
                volume.faces);
        log_info("shadow volume: %u shared edges, %u open edges closed by %u hole loops "
                  "(%u unresolved), %u silhouette edges for the initial light, %u vertices and %u "
                  "indices",
                volume.shared, volume.patched, volume.loops, volume.unresolved,
                countSilhouette(volume, dwarfInverse.TransformPoint(light0)),
                static_cast<unsigned>(volume.vertices.size()),
                static_cast<unsigned>(volume.indices.size()));
    }

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc textureDesc;
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.data = whitePixel;
    textureDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(textureDesc);

    prisma::TextureHandle floorTexture;
    prisma::TextureHandle wallTexture;
    zenapp::mediaPath("misc/cellfloor.dds", path, sizeof(path));
    floorTexture = zenapp::loadTexture(driver, path, true, true);
    zenapp::mediaPath("misc/cellwall.dds", path, sizeof(path));
    wallTexture = zenapp::loadTexture(driver, path, true, true);
    if (!floorTexture.valid()) log_error("room: cellfloor.dds not loaded, using a plain tint");
    if (!wallTexture.valid()) log_error("room: cellwall.dds not loaded, using a plain tint");
    const prisma::TextureHandle roomTextures[4] = { floorTexture.valid() ? floorTexture : white,
        wallTexture.valid() ? wallTexture : white, wallTexture.valid() ? wallTexture : white,
        wallTexture.valid() ? wallTexture : white };

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "room sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

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

    prisma::BufferHandle volumeVertexBuffer;
    prisma::BufferHandle volumeIndexBuffer;
    if (ready)
    {
        bufferDesc.size = static_cast<std::uint32_t>(volume.vertices.size() * sizeof(VolumeVertex));
        bufferDesc.data = volume.vertices.data();
        bufferDesc.debugName = "shadow volume vertices";
        volumeVertexBuffer = driver->createBuffer(bufferDesc);

        bufferDesc.usage = prisma::BufferUsage::Index;
        bufferDesc.indexFormat = prisma::IndexFormat::UInt32;
        bufferDesc.size = static_cast<std::uint32_t>(volume.indices.size() * sizeof(std::uint32_t));
        bufferDesc.data = volume.indices.data();
        bufferDesc.debugName = "shadow volume indices";
        volumeIndexBuffer = driver->createBuffer(bufferDesc);
    }

    const std::uint32_t stride =
            alignUp(sizeof(Block), driver->caps().uniformBufferOffsetAlignment);
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * 3);
    memset(uniforms.data(), 0, uniforms.size());

    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * 3;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "shadow volume uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle sceneVertex = zenapp::createShader(driver, scene_vert);
    const prisma::ShaderHandle ambientFragment = zenapp::createShader(driver, ambient_frag);
    const prisma::ShaderHandle litFragment = zenapp::createShader(driver, lit_frag);
    const prisma::ShaderHandle ballFragment = zenapp::createShader(driver, ball_frag);
    const prisma::ShaderHandle volumeVertex = zenapp::createShader(driver, volume_vert);
    const prisma::ShaderHandle volumeFragment = zenapp::createShader(driver, volume_frag);
    const prisma::ShaderHandle overlayFragment = zenapp::createShader(driver, overlay_frag);

    prisma::PipelineDesc sceneDesc;
    sceneDesc.vertexShader = sceneVertex;
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

    sceneDesc.fragmentShader = ambientFragment;
    sceneDesc.debugName = "ambient";
    const prisma::PipelineHandle ambientPipeline = driver->createPipeline(sceneDesc);

    sceneDesc.fragmentShader = ballFragment;
    sceneDesc.debugName = "light ball";
    const prisma::PipelineHandle ballPipeline = driver->createPipeline(sceneDesc);

    sceneDesc.fragmentShader = litFragment;
    sceneDesc.depthCompare = prisma::CompareOp::LessEqual;
    sceneDesc.depthWrite = false;
    sceneDesc.stencilTest = true;
    sceneDesc.stencilFront.compare = prisma::CompareOp::Equal;
    sceneDesc.stencilBack.compare = prisma::CompareOp::Equal;
    sceneDesc.stencilWriteMask = 0;
    sceneDesc.debugName = "lit outside the shadow";
    const prisma::PipelineHandle litPipeline = driver->createPipeline(sceneDesc);

    prisma::PipelineDesc volumeDesc;
    volumeDesc.vertexShader = volumeVertex;
    volumeDesc.fragmentShader = volumeFragment;
    volumeDesc.vertexBuffers[0].stride = sizeof(VolumeVertex);
    volumeDesc.vertexBufferCount = 1;
    volumeDesc.attributeCount = 2;
    volumeDesc.attributes[0].location = 0;
    volumeDesc.attributes[0].format = prisma::VertexFormat::Float3;
    volumeDesc.attributes[0].offset = 0;
    volumeDesc.attributes[1].location = 1;
    volumeDesc.attributes[1].format = prisma::VertexFormat::Float3;
    volumeDesc.attributes[1].offset = sizeof(float) * 3;
    volumeDesc.depthTest = true;
    volumeDesc.depthWrite = false;
    volumeDesc.cullMode = prisma::CullMode::None;
    volumeDesc.colorMask = 0;
    volumeDesc.stencilTest = true;
    volumeDesc.stencilFront.compare = prisma::CompareOp::Always;
    volumeDesc.stencilFront.depthFailOp = prisma::StencilOp::DecrementWrap;
    volumeDesc.stencilBack.compare = prisma::CompareOp::Always;
    volumeDesc.stencilBack.depthFailOp = prisma::StencilOp::IncrementWrap;
    volumeDesc.debugName = "shadow volume stencil";
    const prisma::PipelineHandle volumePipeline = driver->createPipeline(volumeDesc);

    volumeDesc.fragmentShader = overlayFragment;
    volumeDesc.colorMask = prisma::kColorAll;
    volumeDesc.stencilTest = false;
    volumeDesc.cullMode = prisma::CullMode::Back;
    volumeDesc.blend = true;
    volumeDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    volumeDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    volumeDesc.debugName = "shadow volume overlay";
    const prisma::PipelineHandle overlayPipeline = driver->createPipeline(volumeDesc);

    driver->destroy(sceneVertex);
    driver->destroy(ambientFragment);
    driver->destroy(litFragment);
    driver->destroy(ballFragment);
    driver->destroy(volumeVertex);
    driver->destroy(volumeFragment);
    driver->destroy(overlayFragment);

    ready = ready && white.valid() && sampler.valid() && roomBuffer.valid() &&
            volumeVertexBuffer.valid() && volumeIndexBuffer.valid() && uniformBuffer.valid() &&
            ambientPipeline.valid() && ballPipeline.valid() && litPipeline.valid() &&
            volumePipeline.valid() && overlayPipeline.valid();
    if (!ready) log_error("shadow volume: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;
    pass.clearStencil = 0;

    const std::uint32_t volumeIndexCount = static_cast<std::uint32_t>(volume.indices.size());
    const std::uint32_t blockSize = sizeof(Block);

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_T)) showVolume = !showVolume;

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = still ? kStillTime : static_cast<float>(time_seconds()) + kStillTime;
        const Math::Vec3 light = lightPosition(time);

        const Math::Mat4 projection = zenapp::perspectiveInfiniteZeroToOne(0.9f, aspect, kNear);
        const Math::Mat4 view = Math::Mat4::LookAt(Math::Vec3(0.0f, 2.8f, 6.6f),
                Math::Vec3(0.0f, 1.0f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const Math::Mat4 ballModel = Math::Mat4::Translation(light) *
                                     Math::Mat4::Scale(Math::Vec3(ballScale, ballScale, ballScale)) *
                                     Math::Mat4::Translation(-ballCenter);
        Block block;
        fillBlock(&block, projection, view, Math::Mat4::Identity(), light);
        memcpy(uniforms.data(), &block, sizeof(block));
        fillBlock(&block, projection, view, dwarfModel, light);
        memcpy(uniforms.data() + stride, &block, sizeof(block));
        fillBlock(&block, projection, view, ballModel, light);
        memcpy(uniforms.data() + stride * 2, &block, sizeof(block));

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * 3);
        driver->beginRenderPass(pass);

        driver->bindPipeline(ambientPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, blockSize);
        driver->bindVertexBuffer(0, roomBuffer, 0);
        for (unsigned s = 0; s < 4; ++s)
        {
            driver->bindTexture(0, roomTextures[s], sampler);
            driver->draw(6, s * 6);
        }
        driver->bindUniformBuffer(0, uniformBuffer, stride, blockSize);
        for (unsigned i = 0; i < dwarf.subsetCount(0); ++i)
        {
            prisma::TextureHandle diffuse = dwarf.diffuse(dwarf.subset(0, i).materialId);
            driver->bindTexture(0, diffuse.valid() ? diffuse : white, sampler);
            dwarf.drawSubset(driver, 0, i);
        }

        driver->bindPipeline(ballPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, stride * 2, blockSize);
        driver->bindTexture(0, white, sampler);
        for (unsigned i = 0; i < ball.subsetCount(0); ++i) ball.drawSubset(driver, 0, i);

        driver->bindPipeline(volumePipeline);
        driver->bindUniformBuffer(0, uniformBuffer, stride, blockSize);
        driver->bindVertexBuffer(0, volumeVertexBuffer, 0);
        driver->bindIndexBuffer(volumeIndexBuffer);
        driver->drawIndexed(volumeIndexCount, 0);

        driver->setStencilReference(0);
        driver->bindPipeline(litPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, blockSize);
        driver->bindVertexBuffer(0, roomBuffer, 0);
        for (unsigned s = 0; s < 4; ++s)
        {
            driver->bindTexture(0, roomTextures[s], sampler);
            driver->draw(6, s * 6);
        }
        driver->bindUniformBuffer(0, uniformBuffer, stride, blockSize);
        for (unsigned i = 0; i < dwarf.subsetCount(0); ++i)
        {
            prisma::TextureHandle diffuse = dwarf.diffuse(dwarf.subset(0, i).materialId);
            driver->bindTexture(0, diffuse.valid() ? diffuse : white, sampler);
            dwarf.drawSubset(driver, 0, i);
        }

        if (showVolume)
        {
            driver->bindPipeline(overlayPipeline);
            driver->bindUniformBuffer(0, uniformBuffer, stride, blockSize);
            driver->bindVertexBuffer(0, volumeVertexBuffer, 0);
            driver->bindIndexBuffer(volumeIndexBuffer);
            driver->drawIndexed(volumeIndexCount, 0);
        }

        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(overlayPipeline);
    driver->destroy(volumePipeline);
    driver->destroy(litPipeline);
    driver->destroy(ballPipeline);
    driver->destroy(ambientPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(volumeIndexBuffer);
    driver->destroy(volumeVertexBuffer);
    driver->destroy(roomBuffer);
    driver->destroy(sampler);
    if (floorTexture.valid()) driver->destroy(floorTexture);
    if (wallTexture.valid()) driver->destroy(wallTexture);
    driver->destroy(white);
    ball.destroy(driver);
    dwarf.destroy(driver);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
