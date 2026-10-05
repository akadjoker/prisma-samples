#include "Check.h"
#include "GltfModel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

bool exists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

void checkGeometry(const zenapp::GltfModel& model)
{
    size_t badIndices = 0;
    size_t badNormals = 0;
    size_t badTangents = 0;
    size_t nonFinite = 0;
    size_t badBounds = 0;
    for (const zenapp::GltfPrimitive& primitive: model.primitives)
    {
        CHECK(primitive.firstVertex + primitive.vertexCount <= model.vertices.size());
        CHECK(primitive.firstIndex + primitive.indexCount <= model.indices.size());
        CHECK(primitive.indexCount % 3 == 0);
        CHECK(primitive.material < static_cast<int>(model.materials.size()));
        if (primitive.material >= 0 && model.materials[primitive.material].normalTexture >= 0)
            CHECK(primitive.hasTangents);
        for (uint32_t i = 0; i < primitive.indexCount; ++i)
            if (model.indices[primitive.firstIndex + i] >= primitive.vertexCount) ++badIndices;
        for (uint32_t i = 0; i < primitive.vertexCount; ++i)
        {
            const zenapp::GltfVertex& v = model.vertices[primitive.firstVertex + i];
            const float length = sqrtf(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] +
                                       v.normal[2] * v.normal[2]);
            if (!(fabsf(length - 1.0f) < 1e-2f)) ++badNormals;
            if (primitive.hasTangents)
            {
                const float orthogonal = v.tangent[0] * v.normal[0] + v.tangent[1] * v.normal[1] +
                                         v.tangent[2] * v.normal[2];
                if (fabsf(orthogonal) > 2e-3f) ++badTangents;
                const float tangentLength = sqrtf(v.tangent[0] * v.tangent[0] +
                                                  v.tangent[1] * v.tangent[1] +
                                                  v.tangent[2] * v.tangent[2]);
                if (!(fabsf(tangentLength - 1.0f) < 5e-2f) || fabsf(fabsf(v.tangent[3]) - 1.0f) > 1e-3f)
                    ++badTangents;
            }
            const float* values = v.position;
            for (int k = 0; k < 12; ++k)
                if (!isfinite(values[k])) ++nonFinite;
            for (int c = 0; c < 3; ++c)
                if (v.position[c] < primitive.boundsMin[c] || v.position[c] > primitive.boundsMax[c])
                    ++badBounds;
        }
    }
    CHECK(badIndices == 0);
    CHECK(badNormals == 0);
    CHECK(badTangents == 0);
    CHECK(nonFinite == 0);
    CHECK(badBounds == 0);
    for (const zenapp::GltfNode& node: model.nodes)
    {
        bool finite = true;
        for (int k = 0; k < 16; ++k) finite = finite && isfinite(node.world[k]);
        CHECK(finite);
        CHECK(node.mesh < static_cast<int>(model.meshes.size()));
        CHECK(node.light < static_cast<int>(model.lights.size()));
        CHECK(fabsf(node.world[15] - 1.0f) < 1e-4f);
    }
    for (const zenapp::GltfMesh& mesh: model.meshes)
        CHECK(mesh.firstPrimitive + mesh.primitiveCount <= model.primitives.size());
    for (const zenapp::GltfMaterial& material: model.materials)
    {
        const int textures[5] = { material.baseColorTexture, material.surfaceTexture,
            material.normalTexture, material.occlusionTexture, material.emissiveTexture };
        for (int t: textures) CHECK(t < static_cast<int>(model.textures.size()));
    }
}

} // namespace

int main()
{
    zenapp::GltfModel missing;
    CHECK(!zenapp::loadGltf("does/not/exist.gltf", &missing));
    CHECK(missing.error.size() > 0);

    char path[1024];
    snprintf(path, sizeof(path), "%s/DamagedHelmet/DamagedHelmet.glb", PRISMA_MODELS_DIR);
    if (exists(path))
    {
        zenapp::GltfModel helmet;
        CHECK(zenapp::loadGltf(path, &helmet));
        CHECK(helmet.vertices.size() >= 14000 && helmet.vertices.size() <= 15000);
        CHECK(helmet.indices.size() == 46356);
        CHECK(helmet.primitives.size() == 1);
        CHECK(helmet.meshes.size() == 1);
        CHECK(helmet.nodes.size() == 1);
        CHECK(helmet.materials.size() == 1);
        CHECK(helmet.textures.size() == 5);
        if (helmet.materials.size() == 1)
        {
            const zenapp::GltfMaterial& m = helmet.materials[0];
            CHECK(!m.specularGlossiness);
            CHECK(m.baseColorTexture == 0 && m.surfaceTexture == 1 && m.emissiveTexture == 2);
            CHECK(m.occlusionTexture == 3 && m.normalTexture == 4);
            CHECK(m.emissive[0] == 1.0f);
        }
        CHECK(helmet.primitives[0].hasTangents);
        bool embedded = true;
        for (const zenapp::GltfTexture& texture: helmet.textures)
            embedded = embedded && texture.embedded.size() > 0 && texture.path.size() == 0;
        CHECK(embedded);
        checkGeometry(helmet);
        printf("helmet: %zu vertices, %zu triangles\n", helmet.vertices.size(),
                helmet.indices.size() / 3);
    }
    else
        printf("helmet not found at %s, skipped\n", path);

    const char* bistroPath = getenv("PRISMA_BISTRO");
    if (bistroPath && exists(bistroPath))
    {
        zenapp::GltfModel bistro;
        const bool loaded = zenapp::loadGltf(bistroPath, &bistro);
        if (!loaded) printf("bistro: %s\n", bistro.error.c_str());
        CHECK(loaded);
        CHECK(bistro.primitives.size() == 551);
        CHECK(bistro.vertices.size() <= 1738262 && bistro.vertices.size() > 1500000);
        CHECK(bistro.indices.size() == 5260890);
        CHECK(bistro.meshes.size() == 551);
        CHECK(bistro.materials.size() == 254);
        CHECK(bistro.textures.size() == 343);
        CHECK(bistro.lights.size() == 97);
        CHECK(bistro.nodes.size() == 2909 + 97);
        CHECK(bistro.camera.valid);
        CHECK(fabsf(bistro.camera.yfov - 0.628f) < 1e-3f);
        size_t specularGlossiness = 0;
        size_t blend = 0;
        size_t mask = 0;
        size_t withDds = 0;
        size_t missingFiles = 0;
        for (const zenapp::GltfMaterial& m: bistro.materials)
        {
            specularGlossiness += m.specularGlossiness ? 1 : 0;
            blend += m.alpha == zenapp::GltfMaterial::Alpha::Blend ? 1 : 0;
            mask += m.alpha == zenapp::GltfMaterial::Alpha::Mask ? 1 : 0;
        }
        for (const zenapp::GltfTexture& t: bistro.textures)
        {
            if (t.ddsPath.size() > 0)
            {
                ++withDds;
                if (!exists(t.ddsPath.c_str())) ++missingFiles;
            }
        }
        printf("bistro: %zu spec/gloss, %zu blend, %zu mask materials, %zu textures with a dds, "
               "%zu missing files\n", specularGlossiness, blend, mask, withDds, missingFiles);
        CHECK(specularGlossiness == 234);
        CHECK(blend == 3);
        CHECK(mask == 20);
        CHECK(withDds == 343);
        CHECK(missingFiles == 0);
        checkGeometry(bistro);
        size_t spots = 0;
        size_t points = 0;
        for (const zenapp::GltfLight& light: bistro.lights)
        {
            if (light.type == zenapp::GltfLight::Type::Spot) ++spots;
            if (light.type == zenapp::GltfLight::Type::Point) ++points;
        }
        printf("bistro lights: %zu point, %zu spot\n", points, spots);
    }
    else
        printf("PRISMA_BISTRO is not set, the Bistro checks were skipped\n");

    printf(failures ? "test_gltf: %d failures\n" : "test_gltf: all passed\n", failures);
    return failures ? 1 : 0;
}
