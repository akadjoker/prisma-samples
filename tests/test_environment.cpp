#include "Check.h"
#include "Environment.h"

#include <math.h>
#include <stdio.h>

namespace
{

void bruteForce(const zenapp::EnvironmentFaces& environment, const float* n, float* rgb)
{
    double sum[3] = { 0.0, 0.0, 0.0 };
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned y = 0; y < environment.size; ++y)
        {
            for (unsigned x = 0; x < environment.size; ++x)
            {
                float d[3];
                zenapp::ibl::faceDirection(face, environment.size, x, y, d);
                const float cosine = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
                if (cosine <= 0.0f) continue;
                const float weight = zenapp::ibl::texelSolidAngle(environment.size, x, y) * cosine;
                const float* texel =
                        environment.face(face) + (static_cast<size_t>(y) * environment.size + x) * 4;
                for (int c = 0; c < 3; ++c) sum[c] += static_cast<double>(texel[c]) * weight;
            }
        }
    }
    for (int c = 0; c < 3; ++c) rgb[c] = static_cast<float>(sum[c] / 3.14159265358979);
}

} // namespace

int main()
{
    using namespace zenapp;

    CHECK(ibl::halfToFloat(0x3C00) == 1.0f);
    CHECK(ibl::halfToFloat(0xC000) == -2.0f);
    CHECK(ibl::halfToFloat(0x0001) == 5.960464477539063e-08f);
    CHECK(ibl::halfToFloat(0x7BFF) == 65504.0f);
    CHECK(isinf(ibl::halfToFloat(0x7C00)));
    bool roundTrip = true;
    for (uint32_t h = 0; h < 0x7C00; h += 7)
        roundTrip = roundTrip && ibl::floatToHalf(ibl::halfToFloat(static_cast<uint16_t>(h))) == h;
    CHECK(roundTrip);

    EnvironmentFaces missing;
    CHECK(!loadEnvironmentFaces("does/not/exist.dds", &missing));

    const char* probes[] = { "Light Probes/grace_cross.dds", "Light Probes/stpeters_cross.dds",
        "Light Probes/uffizi_cross.dds" };
    for (const char* name : probes)
    {
        char path[1024];
        mediaPath(name, path, sizeof(path));
        EnvironmentFaces environment;
        const bool loaded = loadEnvironmentFaces(path, &environment);
        CHECK(loaded);
        if (!loaded) continue;
        CHECK(environment.size == 256 || environment.size == 512);
        CHECK(environment.data.size() == static_cast<size_t>(environment.size) * environment.size * 24);

        bool finite = true;
        for (size_t i = 0; i < environment.data.size(); ++i)
            finite = finite && isfinite(environment.data[i]) && environment.data[i] >= 0.0f;
        CHECK(finite);

        float sh[ibl::kShCoefficients][3];
        computeEnvironmentSh(environment, sh);
        const float normals[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
            { 0, 0, 1 }, { 0, 0, -1 } };
        float worstAbsolute = 0.0f;
        float brightest = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            float fromSh[3];
            float exact[3];
            ibl::evaluateIrradianceSh(sh, normals[i], fromSh);
            bruteForce(environment, normals[i], exact);
            for (int c = 0; c < 3; ++c)
            {
                CHECK(isfinite(fromSh[c]));
                const float error = fabsf(fromSh[c] - exact[c]);
                worstAbsolute = error > worstAbsolute ? error : worstAbsolute;
                brightest = exact[c] > brightest ? exact[c] : brightest;
            }
        }
        printf("%s: size %u, worst SH error %.3f of %.3f brightest irradiance\n", name,
                environment.size, worstAbsolute, brightest);
        CHECK(worstAbsolute < 0.2f * brightest);
        CHECK(brightest > 0.01f);
    }

    printf(failures ? "test_environment: %d failures\n" : "test_environment: all passed\n",
            failures);
    return failures ? 1 : 0;
}
