#include "Check.h"
#include "ObjModel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

void writeText(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    if (!file) return;
    fwrite(text, 1, strlen(text), file);
    fclose(file);
}

bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

} // namespace

int main()
{
    writeText("test_obj_scene.mtl",
            "newmtl red\nKd 1 0 0\nmap_Kd textures\\red.png\nmap_bump textures\\red_n.png\n"
            "newmtl leaf\nKd 0.2 0.8 0.2\nmap_Kd textures\\red.png\nmap_d textures\\leaf_a.png\n");
    writeText("test_obj_scene.obj",
            "mtllib test_obj_scene.mtl\n"
            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
            "v 0 0 2\nv 2 0 2\nv 0 2 2\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
            "vn 0 0 1\n"
            "usemtl red\n"
            "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
            "usemtl leaf\n"
            "f 5 6 7\n"
            "usemtl red\n"
            "f -7/-4/-1 -6/-3/-1 -5/-2/-1\n");

    zenapp::GltfModel model;
    CHECK(zenapp::loadObj("test_obj_scene.obj", &model));
    CHECK(model.primitives.size() == 2);
    CHECK(model.meshes.size() == 1 && model.meshes[0].primitiveCount == 2);
    CHECK(model.nodes.size() == 1 && model.nodes[0].mesh == 0);
    CHECK(model.materials.size() == 3);
    CHECK(model.vertices.size() == 7);

    if (model.primitives.size() == 2)
    {
        const zenapp::GltfPrimitive& red = model.primitives[0];
        const zenapp::GltfPrimitive& leaf = model.primitives[1];
        CHECK(red.indexCount == 9 && red.vertexCount == 4);
        CHECK(leaf.indexCount == 3 && leaf.vertexCount == 3);
        CHECK(red.firstVertex == 0 && red.firstIndex == 0);
        CHECK(leaf.firstVertex == 4 && leaf.firstIndex == 9);
        CHECK(near(red.boundsMax[0], 1.0f) && near(red.boundsMax[1], 1.0f) &&
                near(red.boundsMax[2], 0.0f));
        CHECK(near(leaf.boundsMax[0], 2.0f) && near(leaf.boundsMax[2], 2.0f));

        const zenapp::GltfVertex& corner = model.vertices[red.firstVertex + 1];
        CHECK(near(corner.position[0], 1.0f) && near(corner.uv[0], 1.0f) && near(corner.uv[1], 1.0f));
        CHECK(near(corner.normal[2], 1.0f));

        CHECK(red.hasTangents && !leaf.hasTangents);
        for (uint32_t i = 0; i < red.vertexCount; ++i)
        {
            const zenapp::GltfVertex& v = model.vertices[red.firstVertex + i];
            CHECK(near(v.tangent[0], 1.0f) && near(v.tangent[1], 0.0f) && near(v.tangent[2], 0.0f));
            CHECK(near(fabsf(v.tangent[3]), 1.0f));
        }
        for (uint32_t i = 0; i < leaf.vertexCount; ++i)
        {
            const zenapp::GltfVertex& v = model.vertices[leaf.firstVertex + i];
            CHECK(v.tangent[0] == 0.0f && v.tangent[1] == 0.0f && v.tangent[2] == 0.0f &&
                    v.tangent[3] == 0.0f);
        }

        const zenapp::GltfVertex& flat = model.vertices[leaf.firstVertex + 1];
        CHECK(near(flat.position[0], 2.0f) && near(flat.position[2], 2.0f));
        CHECK(near(flat.normal[0], 0.0f) && near(flat.normal[1], 0.0f) && near(flat.normal[2], 1.0f));
        CHECK(near(flat.uv[0], 0.0f) && near(flat.uv[1], 0.0f));
        for (size_t i = 0; i < model.indices.size(); ++i)
        {
            const uint32_t limit = i < 9 ? red.vertexCount : leaf.vertexCount;
            CHECK(model.indices[i] < limit);
        }
    }

    CHECK(model.materials[0].name == "default");
    const zenapp::GltfMaterial& red = model.materials[1];
    const zenapp::GltfMaterial& leaf = model.materials[2];
    CHECK(near(red.baseColor[0], 1.0f) && near(red.baseColor[1], 0.0f));
    CHECK(red.alpha == zenapp::GltfMaterial::Alpha::Opaque && !red.doubleSided);
    CHECK(leaf.alpha == zenapp::GltfMaterial::Alpha::Mask && leaf.doubleSided);
    CHECK(red.baseColorTexture >= 0 && red.baseColorTexture == leaf.baseColorTexture);
    CHECK(red.normalTexture >= 0 && red.normalTexture != red.baseColorTexture);
    CHECK(leaf.normalTexture < 0);
    CHECK(model.textures.size() == 2);
    if (red.baseColorTexture >= 0)
        CHECK(strcmp(model.textures[static_cast<size_t>(red.baseColorTexture)].path.c_str(),
                      "./textures/red.png") == 0);

    zenapp::GltfModel missing;
    CHECK(!zenapp::loadObj("does/not/exist.obj", &missing));

    writeText("test_obj_bad.obj", "v 0 0 0\nf 1 2 3\n");
    zenapp::GltfModel bad;
    CHECK(!zenapp::loadObj("test_obj_bad.obj", &bad));
    writeText("test_obj_empty.obj", "v 0 0 0\n");
    CHECK(!zenapp::loadObj("test_obj_empty.obj", &bad));

    remove("test_obj_scene.obj");
    remove("test_obj_scene.mtl");
    remove("test_obj_bad.obj");
    remove("test_obj_empty.obj");
    if (failures) printf("%d failed\n", failures);
    else
        printf("ok\n");
    return failures ? 1 : 0;
}
