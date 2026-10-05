#include "Check.h"
#include "SdkAnimation.h"

#include <math.h>
#include <stdio.h>

namespace
{

bool finite(const float* m)
{
    for (int i = 0; i < 16; ++i)
        if (!(fabsf(m[i]) < 1e6f)) return false;
    return true;
}

} // namespace

int main()
{
    using namespace zenapp;

    float a[16];
    float b[16];
    float product[16];
    float inverse[16];
    const float q[4] = { 0.0f, 0.7071068f, 0.0f, 0.7071068f };
    sdkmath::rotation(q, a);
    sdkmath::translation(1.0f, 2.0f, 3.0f, b);
    sdkmath::multiply(a, b, product);
    CHECK(sdkmath::invert(product, inverse));
    sdkmath::multiply(product, inverse, a);
    float expected[16];
    sdkmath::identity(expected);
    bool same = true;
    for (int i = 0; i < 16; ++i)
        if (fabsf(a[i] - expected[i]) > 1e-4f) same = false;
    CHECK(same);

    sdkmath::rotation(q, a);
    const float point[3] = { 1.0f, 0.0f, 0.0f };
    const float rotated[3] = { point[0] * a[0] + point[1] * a[4] + point[2] * a[8],
        point[0] * a[1] + point[1] * a[5] + point[2] * a[9],
        point[0] * a[2] + point[1] * a[6] + point[2] * a[10] };
    CHECK(fabsf(rotated[0]) < 1e-4f && fabsf(rotated[2] + 1.0f) < 1e-4f);

    struct Case
    {
        const char* mesh;
        const char* animation;
    };
    const Case cases[] = { { "Soldier/soldier.sdkmesh", "Soldier/soldier.sdkmesh_anim" },
        { "SubD10/sebastian.sdkmesh", "SubD10/sebastian.sdkmesh_anim" },
        { "SubD10/AnimatedHead.sdkmesh", "SubD10/AnimatedHead.sdkmesh_anim" } };
    int found = 0;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
    {
        char meshPath[512];
        char animationPath[512];
        ct::Vector<unsigned char> bytes;
        if (!mediaPath(cases[c].mesh, meshPath, sizeof(meshPath)) ||
                !mediaPath(cases[c].animation, animationPath, sizeof(animationPath)) ||
                !readFile(meshPath, &bytes))
            continue;
        SdkMeshData mesh;
        SdkAnimation animation;
        if (!parseSdkMesh(bytes.data(), bytes.size(), &mesh)) continue;
        ++found;
        CHECK(animation.load(animationPath));
        CHECK(animation.bind(mesh));
        CHECK(animation.duration() > 0.0f);

        bool allFinite = true;
        const double times[] = { 0.0, 0.3, 1.0, 2.5 };
        for (size_t t = 0; t < sizeof(times) / sizeof(times[0]); ++t)
        {
            animation.evaluate(mesh, times[t]);
            for (size_t f = 0; f < mesh.frames.size(); ++f)
                if (!finite(animation.frameMatrix(static_cast<unsigned>(f)))) allFinite = false;
        }
        CHECK(allFinite);

        unsigned nonRigid = 0;
        unsigned checked = 0;
        for (size_t t = 0; t < sizeof(times) / sizeof(times[0]); ++t)
        {
            animation.evaluate(mesh, times[t]);
            for (size_t f = 0; f < mesh.frames.size(); ++f)
            {
                const float* m = animation.frameMatrix(static_cast<unsigned>(f));
                bool rigid = true;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                    {
                        float dot = 0.0f;
                        for (int k = 0; k < 3; ++k) dot += m[i * 4 + k] * m[j * 4 + k];
                        if (fabsf(dot - (i == j ? 1.0f : 0.0f)) > 2e-2f) rigid = false;
                    }
                const float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                                          m[1] * (m[4] * m[10] - m[6] * m[8]) +
                                          m[2] * (m[4] * m[9] - m[5] * m[8]);
                if (fabsf(determinant - 1.0f) > 2e-2f) rigid = false;
                ++checked;
                if (!rigid) ++nonRigid;
            }
        }
        printf("%s: %u of %u final matrices are not rigid\n", cases[c].animation, nonRigid,
                checked);
        CHECK(nonRigid * 100 <= checked * 10);
        printf("%s: %u frames, %u keys, %u fps, %.2f s\n", cases[c].animation,
                static_cast<unsigned>(mesh.frames.size()), animation.keyCount(),
                animation.framesPerSecond(), animation.duration());
    }
    printf("animated meshes: %d of %d\n", found,
            static_cast<int>(sizeof(cases) / sizeof(cases[0])));

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
