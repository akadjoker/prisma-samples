#include "Check.h"
#include "Sh.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>

namespace
{

const unsigned kSize = 48;

template <typename Function>
void project(Function function, float (*sh)[3])
{
    ct::Vector<float> storage;
    storage.resize(static_cast<size_t>(6) * kSize * kSize * 3);
    const float* faces[6];
    for (unsigned face = 0; face < 6; ++face)
    {
        float* base = storage.data() + static_cast<size_t>(face) * kSize * kSize * 3;
        faces[face] = base;
        for (unsigned y = 0; y < kSize; ++y)
        {
            for (unsigned x = 0; x < kSize; ++x)
            {
                float d[3];
                zenapp::ibl::faceDirection(face, kSize, x, y, d);
                const float v = function(d);
                float* texel = base + (static_cast<size_t>(y) * kSize + x) * 3;
                texel[0] = v;
                texel[1] = v * 0.5f;
                texel[2] = 2.0f * v;
            }
        }
    }
    zenapp::ibl::computeIrradianceSh(faces, kSize, 3, sh);
}

template <typename Function>
float worstError(Function expected, const float (*sh)[3])
{
    float worst = 0.0f;
    for (int i = 0; i < 200; ++i)
    {
        const float a = static_cast<float>(i) * 2.399963f;
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 200.0f;
        const float r = sqrtf(1.0f - z * z);
        const float n[3] = { r * cosf(a), r * sinf(a), z };
        float rgb[3];
        zenapp::ibl::evaluateIrradianceSh(sh, n, rgb);
        const float e = expected(n);
        const float errors[3] = { fabsf(rgb[0] - e), fabsf(rgb[1] - e * 0.5f),
            fabsf(rgb[2] - e * 2.0f) };
        for (int c = 0; c < 3; ++c) worst = errors[c] > worst ? errors[c] : worst;
    }
    return worst;
}

} // namespace

int main()
{
    using namespace zenapp::ibl;

    float total = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) total += texelSolidAngle(kSize, x, y);
    CHECK(fabsf(total * 6.0f - 4.0f * 3.14159265f) < 1e-3f);

    for (unsigned face = 0; face < 6; ++face)
    {
        float d[3];
        faceDirection(face, 2, 0, 0, d);
        const unsigned axis = face / 2;
        const float sign = (face & 1u) ? -1.0f : 1.0f;
        CHECK(d[axis] * sign > 0.5f);
    }

    float sh[kShCoefficients][3];

    project([](const float*) { return 0.7f; }, sh);
    CHECK(worstError([](const float*) { return 0.7f; }, sh) < 2e-3f);

    project([](const float* d) { return 0.5f + 0.3f * d[0] - 0.2f * d[1] + 0.4f * d[2]; }, sh);
    CHECK(worstError([](const float* n) {
        return 0.5f + (2.0f / 3.0f) * (0.3f * n[0] - 0.2f * n[1] + 0.4f * n[2]);
    }, sh) < 5e-3f);

    project([](const float* d) { return 1.0f + d[0] * d[1]; }, sh);
    CHECK(worstError([](const float* n) { return 1.0f + 0.25f * n[0] * n[1]; }, sh) < 5e-3f);

    project([](const float* d) { return 1.0f + d[1] * d[2]; }, sh);
    CHECK(worstError([](const float* n) { return 1.0f + 0.25f * n[1] * n[2]; }, sh) < 5e-3f);

    project([](const float* d) { return 1.0f + d[2] * d[0]; }, sh);
    CHECK(worstError([](const float* n) { return 1.0f + 0.25f * n[2] * n[0]; }, sh) < 5e-3f);

    project([](const float* d) { return 1.0f + 3.0f * d[2] * d[2] - 1.0f; }, sh);
    CHECK(worstError([](const float* n) { return 1.0f + 0.25f * (3.0f * n[2] * n[2] - 1.0f); }, sh) <
          5e-3f);

    project([](const float* d) { return 1.0f + d[0] * d[0] - d[1] * d[1]; }, sh);
    CHECK(worstError([](const float* n) { return 1.0f + 0.25f * (n[0] * n[0] - n[1] * n[1]); }, sh) <
          5e-3f);

    project([](const float* d) { return d[1] > 0.0f ? 1.0f : 0.0f; }, sh);
    float up[3];
    float down[3];
    const float nUp[3] = { 0.0f, 1.0f, 0.0f };
    const float nDown[3] = { 0.0f, -1.0f, 0.0f };
    evaluateIrradianceSh(sh, nUp, up);
    evaluateIrradianceSh(sh, nDown, down);
    CHECK(up[0] > 0.9f && up[0] < 1.1f);
    CHECK(down[0] < 0.2f);

    printf(failures ? "test_sh: %d failures\n" : "test_sh: all passed\n", failures);
    return failures ? 1 : 0;
}
