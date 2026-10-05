#pragma once

#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <stdint.h>

namespace zenapp
{

struct GltfVertex
{
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
};

struct GltfTexture
{
    ct::String path;
    ct::String ddsPath;
    ct::Vector<unsigned char> embedded;
    bool repeat = true;
};

struct GltfMaterial
{
    enum class Alpha
    {
        Opaque,
        Mask,
        Blend
    };

    ct::String name;
    bool specularGlossiness = false;
    float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic = 1.0f;
    float roughness = 1.0f;
    float specular[3] = { 1.0f, 1.0f, 1.0f };
    float glossiness = 1.0f;
    float emissive[3] = { 0.0f, 0.0f, 0.0f };
    float alphaCutoff = 0.5f;
    float normalScale = 1.0f;
    float transmission = 0.0f;
    Alpha alpha = Alpha::Opaque;
    bool doubleSided = false;
    int baseColorTexture = -1;
    int surfaceTexture = -1;
    int normalTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;
};

struct GltfPrimitive
{
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int material = -1;
    bool hasTangents = false;
    float boundsMin[3] = { 0.0f, 0.0f, 0.0f };
    float boundsMax[3] = { 0.0f, 0.0f, 0.0f };
};

struct GltfMesh
{
    ct::String name;
    uint32_t firstPrimitive = 0;
    uint32_t primitiveCount = 0;
};

struct GltfNode
{
    float world[16];
    int mesh = -1;
    int light = -1;
    ct::String name;
};

struct GltfLight
{
    enum class Type
    {
        Directional,
        Point,
        Spot
    };

    Type type = Type::Point;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float direction[3] = { 0.0f, 0.0f, -1.0f };
    float color[3] = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 0.0f;
    float innerCone = 0.0f;
    float outerCone = 0.7853982f;
};

struct GltfCamera
{
    bool valid = false;
    float world[16];
    float yfov = 0.9f;
    float aspect = 1.7777778f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct GltfModel
{
    ct::String directory;
    ct::String error;
    ct::Vector<GltfVertex> vertices;
    ct::Vector<uint32_t> indices;
    ct::Vector<GltfPrimitive> primitives;
    ct::Vector<GltfMesh> meshes;
    ct::Vector<GltfMaterial> materials;
    ct::Vector<GltfTexture> textures;
    ct::Vector<GltfNode> nodes;
    ct::Vector<GltfLight> lights;
    GltfCamera camera;
};

bool loadGltf(const char* path, GltfModel* out);

bool generateTangents(ct::Vector<GltfVertex>* vertices, ct::Vector<uint32_t>* indices);

void orthogonalizeTangent(GltfVertex* vertex);

} // namespace zenapp
