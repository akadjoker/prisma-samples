#pragma once

#include "Media.h"
#include "TextureLoader.h"
#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

#include <stdint.h>
#include <string.h>

namespace zenapp
{

#pragma pack(push, 8)

struct SdkHeader
{
    uint32_t version;
    uint8_t isBigEndian;
    uint64_t headerSize;
    uint64_t nonBufferDataSize;
    uint64_t bufferDataSize;
    uint32_t numVertexBuffers;
    uint32_t numIndexBuffers;
    uint32_t numMeshes;
    uint32_t numTotalSubsets;
    uint32_t numFrames;
    uint32_t numMaterials;
    uint64_t vertexStreamHeadersOffset;
    uint64_t indexStreamHeadersOffset;
    uint64_t meshDataOffset;
    uint64_t subsetDataOffset;
    uint64_t frameDataOffset;
    uint64_t materialDataOffset;
};

struct SdkVertexElement
{
    uint16_t stream;
    uint16_t offset;
    uint8_t type;
    uint8_t method;
    uint8_t usage;
    uint8_t usageIndex;
};

struct SdkVertexBuffer
{
    uint64_t numVertices;
    uint64_t sizeBytes;
    uint64_t strideBytes;
    SdkVertexElement decl[32];
    uint64_t dataOffset;
};

struct SdkIndexBuffer
{
    uint64_t numIndices;
    uint64_t sizeBytes;
    uint32_t indexType;
    uint64_t dataOffset;
};

struct SdkMeshHeader
{
    char name[100];
    uint8_t numVertexBuffers;
    uint32_t vertexBuffers[16];
    uint32_t indexBuffer;
    uint32_t numSubsets;
    uint32_t numFrameInfluences;
    float boundingBoxCenter[3];
    float boundingBoxExtents[3];
    uint64_t subsetOffset;
    uint64_t frameInfluenceOffset;
};

struct SdkSubset
{
    char name[100];
    uint32_t materialId;
    uint32_t primitiveType;
    uint64_t indexStart;
    uint64_t indexCount;
    uint64_t vertexStart;
    uint64_t vertexCount;
};

struct SdkFrame
{
    char name[100];
    uint32_t mesh;
    uint32_t parentFrame;
    uint32_t childFrame;
    uint32_t siblingFrame;
    float matrix[16];
    uint32_t animationDataIndex;
};

struct SdkMaterial
{
    char name[100];
    char materialInstancePath[260];
    char diffuseTexture[260];
    char normalTexture[260];
    char specularTexture[260];
    float diffuse[4];
    float ambient[4];
    float specular[4];
    float emissive[4];
    float power;
    uint64_t unused[6];
};

#pragma pack(pop)

static_assert(sizeof(SdkHeader) == 104, "SDK mesh header size");
static_assert(sizeof(SdkVertexElement) == 8, "SDK mesh vertex element size");
static_assert(sizeof(SdkVertexBuffer) == 288, "SDK mesh vertex buffer size");
static_assert(sizeof(SdkIndexBuffer) == 32, "SDK mesh index buffer size");
static_assert(sizeof(SdkMeshHeader) == 224, "SDK mesh mesh size");
static_assert(sizeof(SdkSubset) == 144, "SDK mesh subset size");
static_assert(sizeof(SdkFrame) == 184, "SDK mesh frame size");
static_assert(sizeof(SdkMaterial) == 1256, "SDK mesh material size");

enum SdkLocation : unsigned
{
    kSdkPosition = 0,
    kSdkNormal = 1,
    kSdkTexCoord0 = 2,
    kSdkTangent = 3,
    kSdkBinormal = 4,
    kSdkColor = 5,
    kSdkBlendWeight = 6,
    kSdkBlendIndices = 7,
    kSdkTexCoord1 = 8,
    kSdkTexCoord2 = 9,
    kSdkTexCoord3 = 10
};

inline bool sdkLocation(unsigned usage, unsigned usageIndex, unsigned* location)
{
    switch (usage)
    {
        case 0:
            *location = kSdkPosition;
            return usageIndex == 0;
        case 1:
            *location = kSdkBlendWeight;
            return usageIndex == 0;
        case 2:
            *location = kSdkBlendIndices;
            return usageIndex == 0;
        case 3:
            *location = kSdkNormal;
            return usageIndex == 0;
        case 5:
            if (usageIndex > 3) return false;
            *location = usageIndex == 0 ? kSdkTexCoord0 : kSdkTexCoord1 + usageIndex - 1;
            return true;
        case 6:
            *location = kSdkTangent;
            return usageIndex == 0;
        case 7:
            *location = kSdkBinormal;
            return usageIndex == 0;
        case 10:
            *location = kSdkColor;
            return usageIndex == 0;
        default:
            return false;
    }
}

inline bool sdkVertexFormat(unsigned type, prisma::VertexFormat* format)
{
    using prisma::VertexFormat;
    switch (type)
    {
        case 0:
            *format = VertexFormat::Float1;
            return true;
        case 1:
            *format = VertexFormat::Float2;
            return true;
        case 2:
            *format = VertexFormat::Float3;
            return true;
        case 3:
            *format = VertexFormat::Float4;
            return true;
        case 4:
        case 8:
            *format = VertexFormat::UByte4Norm;
            return true;
        case 5:
            *format = VertexFormat::UByte4;
            return true;
        case 9:
            *format = VertexFormat::Short2Norm;
            return true;
        case 10:
            *format = VertexFormat::Short4Norm;
            return true;
        case 11:
            *format = VertexFormat::UShort2Norm;
            return true;
        case 12:
            *format = VertexFormat::UShort4Norm;
            return true;
        case 14:
            *format = VertexFormat::Int1010102Norm;
            return true;
        case 15:
            *format = VertexFormat::Half2;
            return true;
        case 16:
            *format = VertexFormat::Half4;
            return true;
        default:
            return false;
    }
}

inline bool sdkTopology(unsigned type, prisma::Topology* topology, unsigned* patchPoints)
{
    using prisma::Topology;
    *patchPoints = 0;
    switch (type)
    {
        case 0:
            *topology = Topology::Triangles;
            return true;
        case 1:
            *topology = Topology::TriangleStrip;
            return true;
        case 2:
            *topology = Topology::Lines;
            return true;
        case 3:
            *topology = Topology::LineStrip;
            return true;
        case 4:
            *topology = Topology::Points;
            return true;
        case 5:
            *topology = Topology::TrianglesAdjacency;
            return true;
        case 6:
            *topology = Topology::TriangleStripAdjacency;
            return true;
        case 7:
            *topology = Topology::LinesAdjacency;
            return true;
        case 8:
            *topology = Topology::LineStripAdjacency;
            return true;
        case 9:
            *topology = Topology::Patches;
            *patchPoints = 4;
            return true;
        case 10:
            *topology = Topology::Patches;
            *patchPoints = 3;
            return true;
        default:
            return false;
    }
}

struct SdkMeshData
{
    ct::Vector<unsigned char> file;
    SdkHeader header;
    ct::Vector<SdkVertexBuffer> vertexBuffers;
    ct::Vector<SdkIndexBuffer> indexBuffers;
    ct::Vector<SdkMeshHeader> meshes;
    ct::Vector<SdkSubset> subsets;
    ct::Vector<SdkFrame> frames;
    ct::Vector<SdkMaterial> materials;
    ct::Vector<uint32_t> meshSubsetIds;
    ct::Vector<uint32_t> meshFirstSubset;
    ct::Vector<uint32_t> influenceIds;
    ct::Vector<uint32_t> meshFirstInfluence;
};

template<typename T>
inline bool sdkReadArray(const ct::Vector<unsigned char>& file, uint64_t offset, uint64_t count,
        ct::Vector<T>* out)
{
    const uint64_t bytes = count * sizeof(T);
    if (count > (1u << 24) || offset > file.size() || bytes > file.size() - offset) return false;
    out->resize(static_cast<size_t>(count));
    if (count) memcpy(out->data(), file.data() + offset, static_cast<size_t>(bytes));
    return true;
}

inline bool parseSdkMesh(const unsigned char* bytes, size_t size, SdkMeshData* mesh)
{
    if (size < sizeof(SdkHeader)) return false;
    mesh->file.resize(size);
    memcpy(mesh->file.data(), bytes, size);
    memcpy(&mesh->header, bytes, sizeof(SdkHeader));
    const SdkHeader& h = mesh->header;
    if (h.version != 101 || h.isBigEndian) return false;
    const uint64_t bufferStart = h.headerSize + h.nonBufferDataSize;
    if (bufferStart > size) return false;

    if (!sdkReadArray(mesh->file, h.vertexStreamHeadersOffset, h.numVertexBuffers,
                &mesh->vertexBuffers) ||
            !sdkReadArray(mesh->file, h.indexStreamHeadersOffset, h.numIndexBuffers,
                    &mesh->indexBuffers) ||
            !sdkReadArray(mesh->file, h.meshDataOffset, h.numMeshes, &mesh->meshes) ||
            !sdkReadArray(mesh->file, h.subsetDataOffset, h.numTotalSubsets, &mesh->subsets) ||
            !sdkReadArray(mesh->file, h.frameDataOffset, h.numFrames, &mesh->frames) ||
            !sdkReadArray(mesh->file, h.materialDataOffset, h.numMaterials, &mesh->materials))
        return false;

    for (size_t i = 0; i < mesh->vertexBuffers.size(); ++i)
    {
        const SdkVertexBuffer& vb = mesh->vertexBuffers[i];
        if (vb.dataOffset > size || vb.sizeBytes > size - vb.dataOffset || vb.strideBytes == 0)
            return false;
    }
    for (size_t i = 0; i < mesh->indexBuffers.size(); ++i)
    {
        const SdkIndexBuffer& ib = mesh->indexBuffers[i];
        const uint64_t indexSize = ib.indexType == 1 ? 4 : 2;
        if (ib.dataOffset > size || ib.sizeBytes > size - ib.dataOffset ||
                ib.numIndices * indexSize > ib.sizeBytes)
            return false;
    }

    for (size_t m = 0; m < mesh->meshes.size(); ++m)
    {
        const SdkMeshHeader& part = mesh->meshes[m];
        if (part.numVertexBuffers > 16 || part.indexBuffer >= mesh->indexBuffers.size())
            return false;
        for (unsigned v = 0; v < part.numVertexBuffers; ++v)
            if (part.vertexBuffers[v] >= mesh->vertexBuffers.size()) return false;

        ct::Vector<uint32_t> list;
        if (!sdkReadArray(mesh->file, part.subsetOffset, part.numSubsets, &list)) return false;
        mesh->meshFirstSubset.push_back(static_cast<uint32_t>(mesh->meshSubsetIds.size()));
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] >= mesh->subsets.size()) return false;
            mesh->meshSubsetIds.push_back(list[i]);
        }
        ct::Vector<uint32_t> bones;
        if (!sdkReadArray(mesh->file, part.frameInfluenceOffset, part.numFrameInfluences, &bones))
            return false;
        mesh->meshFirstInfluence.push_back(static_cast<uint32_t>(mesh->influenceIds.size()));
        for (size_t i = 0; i < bones.size(); ++i) mesh->influenceIds.push_back(bones[i]);
    }
    for (size_t s = 0; s < mesh->subsets.size(); ++s)
    {
        prisma::Topology topology;
        unsigned patchPoints;
        if (!sdkTopology(mesh->subsets[s].primitiveType, &topology, &patchPoints)) return false;
    }
    return true;
}

inline void sdkDirectory(const char* path, char* out, size_t size)
{
    snprintf(out, size, "%s", path);
    char* slash = strrchr(out, '/');
    if (slash) slash[1] = '\0';
    else
        out[0] = '\0';
}

class SdkMesh
{
public:
    bool load(prisma::Driver* driver, const char* path, bool loadTextures = true)
    {
        ct::Vector<unsigned char> bytes;
        if (!readFile(path, &bytes) || !parseSdkMesh(bytes.data(), bytes.size(), &data_))
            return false;
        for (size_t i = 0; i < data_.vertexBuffers.size(); ++i)
        {
            const SdkVertexBuffer& vb = data_.vertexBuffers[i];
            prisma::BufferDesc desc;
            desc.usage = prisma::BufferUsage::Vertex;
            desc.size = static_cast<uint32_t>(vb.sizeBytes);
            desc.data = data_.file.data() + vb.dataOffset;
            desc.debugName = path;
            vertexBuffers_.push_back(driver->createBuffer(desc));
            if (!vertexBuffers_[i].valid()) return false;
        }
        for (size_t i = 0; i < data_.indexBuffers.size(); ++i)
        {
            const SdkIndexBuffer& ib = data_.indexBuffers[i];
            prisma::BufferDesc desc;
            desc.usage = prisma::BufferUsage::Index;
            desc.indexFormat =
                    ib.indexType == 1 ? prisma::IndexFormat::UInt32 : prisma::IndexFormat::UInt16;
            desc.size = static_cast<uint32_t>(ib.sizeBytes);
            desc.data = data_.file.data() + ib.dataOffset;
            desc.debugName = path;
            indexBuffers_.push_back(driver->createBuffer(desc));
            if (!indexBuffers_[i].valid()) return false;
        }
        if (loadTextures) loadMaterialTextures(driver, path);
        return true;
    }

    void destroy(prisma::Driver* driver)
    {
        for (size_t i = 0; i < vertexBuffers_.size(); ++i) driver->destroy(vertexBuffers_[i]);
        for (size_t i = 0; i < indexBuffers_.size(); ++i) driver->destroy(indexBuffers_[i]);
        for (size_t i = 0; i < textures_.size(); ++i) driver->destroy(textures_[i]);
        vertexBuffers_.clear();
        indexBuffers_.clear();
        textures_.clear();
    }

    const SdkMeshData& data() const { return data_; }
    unsigned meshCount() const { return static_cast<unsigned>(data_.meshes.size()); }
    unsigned materialCount() const { return static_cast<unsigned>(data_.materials.size()); }
    unsigned subsetCount(unsigned mesh) const { return data_.meshes[mesh].numSubsets; }

    const SdkSubset& subset(unsigned mesh, unsigned index) const
    {
        return data_.subsets[data_.meshSubsetIds[data_.meshFirstSubset[mesh] + index]];
    }

    const SdkMaterial& material(unsigned index) const { return data_.materials[index]; }

    prisma::TextureHandle diffuse(unsigned material) const
    {
        return material < diffuse_.size() ? diffuse_[material] : prisma::TextureHandle();
    }
    prisma::TextureHandle normal(unsigned material) const
    {
        return material < normal_.size() ? normal_[material] : prisma::TextureHandle();
    }
    prisma::TextureHandle specular(unsigned material) const
    {
        return material < specular_.size() ? specular_[material] : prisma::TextureHandle();
    }

    bool layout(unsigned mesh, prisma::PipelineDesc* desc) const
    {
        const SdkMeshHeader& part = data_.meshes[mesh];
        desc->vertexBufferCount = part.numVertexBuffers;
        if (part.numVertexBuffers > prisma::PipelineDesc::kMaxVertexBuffers) return false;
        for (unsigned s = 0; s < part.numVertexBuffers; ++s)
        {
            desc->vertexBuffers[s].stride =
                    static_cast<uint32_t>(data_.vertexBuffers[part.vertexBuffers[s]].strideBytes);
            desc->vertexBuffers[s].step = prisma::VertexStep::Vertex;
        }
        unsigned count = 0;
        const SdkVertexBuffer& first = data_.vertexBuffers[part.vertexBuffers[0]];
        for (unsigned e = 0; e < 32 && first.decl[e].stream != 0xFF; ++e)
        {
            const SdkVertexElement& element = first.decl[e];
            unsigned location = 0;
            prisma::VertexFormat format;
            if (!sdkLocation(element.usage, element.usageIndex, &location)) continue;
            if (!sdkVertexFormat(element.type, &format) ||
                    element.stream >= part.numVertexBuffers ||
                    count >= prisma::PipelineDesc::kMaxAttributes)
                return false;
            prisma::VertexAttribute& attribute = desc->attributes[count++];
            attribute.location = location;
            attribute.format = format;
            attribute.offset = element.offset;
            attribute.buffer = element.stream;
        }
        desc->attributeCount = count;
        return true;
    }

    void drawSubset(prisma::Driver* driver, unsigned mesh, unsigned index) const
    {
        const SdkMeshHeader& part = data_.meshes[mesh];
        const SdkSubset& sub = subset(mesh, index);
        for (unsigned s = 0; s < part.numVertexBuffers; ++s)
        {
            const uint32_t buffer = part.vertexBuffers[s];
            driver->bindVertexBuffer(s, vertexBuffers_[buffer],
                    static_cast<uint32_t>(
                            sub.vertexStart * data_.vertexBuffers[buffer].strideBytes));
        }
        driver->bindIndexBuffer(indexBuffers_[part.indexBuffer]);
        driver->drawIndexed(static_cast<uint32_t>(sub.indexCount),
                static_cast<uint32_t>(sub.indexStart));
    }

private:
    void loadMaterialTextures(prisma::Driver* driver, const char* path)
    {
        char directory[512];
        sdkDirectory(path, directory, sizeof(directory));
        for (size_t m = 0; m < data_.materials.size(); ++m)
        {
            diffuse_.push_back(loadOne(driver, directory, data_.materials[m].diffuseTexture, true));
            normal_.push_back(loadOne(driver, directory, data_.materials[m].normalTexture, false));
            specular_.push_back(
                    loadOne(driver, directory, data_.materials[m].specularTexture, false));
        }
    }

    prisma::TextureHandle loadOne(prisma::Driver* driver, const char* directory, const char* name,
            bool srgb)
    {
        if (!name[0] || strcmp(name, "NULL") == 0) return prisma::TextureHandle();
        char full[1024];
        snprintf(full, sizeof(full), "%s%s", directory, name);
        for (char* c = full; *c; ++c)
            if (*c == '\\') *c = '/';
        prisma::TextureHandle texture = loadTexture(driver, full, srgb, true);
        if (texture.valid()) textures_.push_back(texture);
        return texture;
    }

    SdkMeshData data_;
    ct::Vector<prisma::BufferHandle> vertexBuffers_;
    ct::Vector<prisma::BufferHandle> indexBuffers_;
    ct::Vector<prisma::TextureHandle> diffuse_;
    ct::Vector<prisma::TextureHandle> normal_;
    ct::Vector<prisma::TextureHandle> specular_;
    ct::Vector<prisma::TextureHandle> textures_;
};

} // namespace zenapp
