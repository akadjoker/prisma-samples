#include "Check.h"
#include "SdkMesh.h"

#include <stdio.h>

int main()
{
    using namespace prisma;

    DriverDesc desc;
    desc.type = DriverType::Null;
    Driver* driver = createDriver(desc);
    CHECK(driver != nullptr);
    if (!driver) return 1;

    const char* const files[] = { "Tiny/tiny.sdkmesh", "Tiger/tiger.sdkmesh",
        "Teapot/Teapot.sdkmesh", "ExoticCar/exoticcar.sdkmesh", "ColumnScene/scene.sdkmesh",
        "ColumnScene/Poles.sdkmesh", "misc/ball.sdkmesh", "BlackHoleRoom/blackholeroom.sdkmesh",
        "Squid/Squid.sdkmesh", "SquidRoom/SquidRoom.sdkmesh", "powerplant/powerplant.sdkmesh",
        "ShadowColumns/testscene.sdkmesh", "CloudBox/skysphere.sdkmesh",
        "IslandScene/island.sdkmesh", "trees/tree.sdkmesh", "Soldier/soldier.sdkmesh",
        "SubD10/sebastian.sdkmesh" };
    int found = 0;
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); ++f)
    {
        char path[512];
        if (!zenapp::mediaPath(files[f], path, sizeof(path))) continue;
        ct::Vector<unsigned char> probe;
        if (!zenapp::readFile(path, &probe)) continue;
        ++found;

        zenapp::SdkMesh mesh;
        const bool loaded = mesh.load(driver, path);
        if (!loaded) printf("could not load %s\n", path);
        CHECK(loaded);
        if (!loaded) continue;
        CHECK(mesh.meshCount() > 0);

        const zenapp::SdkMeshData& data = mesh.data();
        bool indicesInRange = true;
        bool layoutsValid = true;
        unsigned totalSubsets = 0;
        for (unsigned m = 0; m < mesh.meshCount(); ++m)
        {
            PipelineDesc layout;
            if (!mesh.layout(m, &layout) || layout.attributeCount == 0) layoutsValid = false;
            const zenapp::SdkMeshHeader& part = data.meshes[m];
            const zenapp::SdkIndexBuffer& ib = data.indexBuffers[part.indexBuffer];
            const unsigned char* indices = data.file.data() + ib.dataOffset;
            for (unsigned s = 0; s < mesh.subsetCount(m); ++s)
            {
                ++totalSubsets;
                const zenapp::SdkSubset& sub = mesh.subset(m, s);
                if (sub.indexStart + sub.indexCount > ib.numIndices) indicesInRange = false;
                if (sub.materialId != 0xFFFFFFFF && sub.materialId >= mesh.materialCount())
                    indicesInRange = false;
                if (sub.primitiveType != 0 && sub.primitiveType != 1) continue;
                for (uint64_t i = sub.indexStart; i < sub.indexStart + sub.indexCount; ++i)
                {
                    const uint32_t index = ib.indexType == 1
                                                   ? reinterpret_cast<const uint32_t*>(indices)[i]
                                                   : reinterpret_cast<const uint16_t*>(indices)[i];
                    const uint64_t vertices = data.vertexBuffers[part.vertexBuffers[0]].numVertices;
                    if (sub.vertexStart + index >= vertices && index != 0xFFFF &&
                            index != 0xFFFFFFFF)
                        indicesInRange = false;
                }
            }
        }
        char directory[512];
        zenapp::sdkDirectory(path, directory, sizeof(directory));
        int missing = 0;
        for (unsigned i = 0; i < mesh.materialCount(); ++i)
        {
            const char* const names[3] = { mesh.material(i).diffuseTexture,
                mesh.material(i).normalTexture, mesh.material(i).specularTexture };
            for (int n = 0; n < 3; ++n)
            {
                if (!names[n][0] || strcmp(names[n], "NULL") == 0 ||
                        strncmp(names[n], "default", 7) == 0)
                    continue;
                char full[1024];
                char found[1024];
                snprintf(full, sizeof(full), "%s%s", directory, names[n]);
                for (char* c = full; *c; ++c)
                    if (*c == '\\') *c = '/';
                if (!zenapp::resolveCase(full, found, sizeof(found)))
                {
                    ++missing;
                    printf("  missing texture %s\n", full);
                }
            }
        }
        if (missing) printf("%s: %d textures not found\n", files[f], missing);
        CHECK(missing == 0);
        if (!indicesInRange) printf("indices out of range in %s\n", path);
        if (!layoutsValid) printf("vertex layout not mapped in %s\n", path);
        CHECK(indicesInRange);
        CHECK(layoutsValid);
        printf("%s: %u meshes, %u subsets, %u materials\n", files[f], mesh.meshCount(),
                totalSubsets, mesh.materialCount());
        mesh.destroy(driver);
    }
    printf("sdkmesh files loaded: %d of %d\n", found,
            static_cast<int>(sizeof(files) / sizeof(files[0])));

    zenapp::SdkMeshData broken;
    unsigned char garbage[512] = { 1, 2, 3 };
    CHECK(!zenapp::parseSdkMesh(garbage, sizeof(garbage), &broken));
    CHECK(!zenapp::parseSdkMesh(garbage, 10, &broken));

    destroyDriver(driver);
    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
