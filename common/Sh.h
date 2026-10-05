#pragma once

#include <math.h>
#include <stddef.h>

namespace zenapp
{

namespace ibl
{

enum
{
    kShBands = 3,
    kShCoefficients = kShBands * kShBands
};

inline unsigned shIndex(int m, unsigned l)
{
    return static_cast<unsigned>(static_cast<int>(l * (l + 1)) + m);
}

inline float factorialRatio(unsigned n, unsigned d)
{
    d = d < 1 ? 1 : d;
    n = n < 1 ? 1 : n;
    float r = 1.0f;
    if (n > d)
    {
        for (; n > d; --n) r *= static_cast<float>(n);
    }
    else if (n < d)
    {
        for (; d > n; --d) r *= static_cast<float>(d);
        r = 1.0f / r;
    }
    return r;
}

inline float shScale(int m, unsigned l)
{
    const unsigned absM = static_cast<unsigned>(m < 0 ? -m : m);
    const float k = static_cast<float>(2 * l + 1) * factorialRatio(l - absM, l + absM);
    return sqrtf(k) * (1.12837916709551f * 0.25f);
}

inline float truncatedCosSh(unsigned l)
{
    const float pi = 3.14159265358979f;
    if (l == 0) return pi;
    if (l == 1) return 2.0f * pi / 3.0f;
    if (l & 1u) return 0.0f;
    const unsigned half = l / 2;
    const float a0 = ((half & 1u) ? 1.0f : -1.0f) / static_cast<float>((l + 2) * (l - 1));
    const float a1 = factorialRatio(l, half) /
                     (factorialRatio(half, 1) * static_cast<float>(1u << l));
    return 2.0f * pi * a0 * a1;
}

inline void shBasis(const float* s, float* basis)
{
    float pml2 = 0.0f;
    float pml1 = 1.0f;
    basis[0] = pml1;
    for (unsigned l = 1; l < kShBands; ++l)
    {
        const float pml = ((2.0f * l - 1.0f) * pml1 * s[2] - (l - 1.0f) * pml2) / l;
        pml2 = pml1;
        pml1 = pml;
        basis[shIndex(0, l)] = pml;
    }
    float pmm = 1.0f;
    for (unsigned m = 1; m < kShBands; ++m)
    {
        pmm = (1.0f - 2.0f * m) * pmm;
        pml2 = pmm;
        pml1 = (2.0f * m + 1.0f) * pmm * s[2];
        basis[shIndex(-static_cast<int>(m), m)] = pml2;
        basis[shIndex(static_cast<int>(m), m)] = pml2;
        if (m + 1 < kShBands)
        {
            basis[shIndex(-static_cast<int>(m), m + 1)] = pml1;
            basis[shIndex(static_cast<int>(m), m + 1)] = pml1;
            for (unsigned l = m + 2; l < kShBands; ++l)
            {
                const float pml = ((2.0f * l - 1.0f) * pml1 * s[2] - (l + m - 1.0f) * pml2) / (l - m);
                pml2 = pml1;
                pml1 = pml;
                basis[shIndex(-static_cast<int>(m), l)] = pml;
                basis[shIndex(static_cast<int>(m), l)] = pml;
            }
        }
    }
    float cm = s[0];
    float sm = s[1];
    for (unsigned m = 1; m <= kShBands; ++m)
    {
        for (unsigned l = m; l < kShBands; ++l)
        {
            basis[shIndex(-static_cast<int>(m), l)] *= sm;
            basis[shIndex(static_cast<int>(m), l)] *= cm;
        }
        const float cm1 = cm * s[0] - sm * s[1];
        const float sm1 = sm * s[0] + cm * s[1];
        cm = cm1;
        sm = sm1;
    }
}

inline float sphereQuadrantArea(float x, float y)
{
    return atan2f(x * y, sqrtf(x * x + y * y + 1.0f));
}

inline float texelSolidAngle(unsigned size, unsigned x, unsigned y)
{
    const float inverse = 1.0f / static_cast<float>(size);
    const float s = (static_cast<float>(x) + 0.5f) * 2.0f * inverse - 1.0f;
    const float t = (static_cast<float>(y) + 0.5f) * 2.0f * inverse - 1.0f;
    const float x0 = s - inverse;
    const float y0 = t - inverse;
    const float x1 = s + inverse;
    const float y1 = t + inverse;
    return sphereQuadrantArea(x0, y0) - sphereQuadrantArea(x0, y1) - sphereQuadrantArea(x1, y0) +
           sphereQuadrantArea(x1, y1);
}

inline void faceDirection(unsigned face, unsigned size, unsigned x, unsigned y, float* d)
{
    const float s = (static_cast<float>(x) + 0.5f) * 2.0f / static_cast<float>(size) - 1.0f;
    const float t = (static_cast<float>(y) + 0.5f) * 2.0f / static_cast<float>(size) - 1.0f;
    switch (face)
    {
        case 0: d[0] = 1.0f; d[1] = -t; d[2] = -s; break;
        case 1: d[0] = -1.0f; d[1] = -t; d[2] = s; break;
        case 2: d[0] = s; d[1] = 1.0f; d[2] = t; break;
        case 3: d[0] = s; d[1] = -1.0f; d[2] = -t; break;
        case 4: d[0] = s; d[1] = -t; d[2] = 1.0f; break;
        default: d[0] = -s; d[1] = -t; d[2] = -1.0f; break;
    }
    const float inverseLength = 1.0f / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    d[0] *= inverseLength;
    d[1] *= inverseLength;
    d[2] *= inverseLength;
}

inline void computeIrradianceSh(const float* const* faces, unsigned size, unsigned channels,
        float (*sh)[3])
{
    for (unsigned i = 0; i < kShCoefficients; ++i) sh[i][0] = sh[i][1] = sh[i][2] = 0.0f;

    float basis[kShCoefficients];
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned y = 0; y < size; ++y)
        {
            for (unsigned x = 0; x < size; ++x)
            {
                float d[3];
                faceDirection(face, size, x, y, d);
                const float weight = texelSolidAngle(size, x, y);
                const float* texel = faces[face] + (static_cast<size_t>(y) * size + x) * channels;
                shBasis(d, basis);
                for (unsigned i = 0; i < kShCoefficients; ++i)
                {
                    sh[i][0] += texel[0] * weight * basis[i];
                    sh[i][1] += texel[1] * weight * basis[i];
                    sh[i][2] += texel[2] * weight * basis[i];
                }
            }
        }
    }

    float scale[kShCoefficients];
    for (unsigned l = 0; l < kShBands; ++l)
    {
        const float cosine = truncatedCosSh(l);
        scale[shIndex(0, l)] = shScale(0, l) * cosine;
        for (unsigned m = 1; m <= l; ++m)
        {
            const float k = 1.41421356237310f * shScale(static_cast<int>(m), l) * cosine;
            scale[shIndex(-static_cast<int>(m), l)] = k;
            scale[shIndex(static_cast<int>(m), l)] = k;
        }
    }

    const float sqrtPi = 1.7724538509f;
    const float sqrt3 = 1.7320508076f;
    const float sqrt5 = 2.2360679775f;
    const float sqrt15 = 3.8729833462f;
    const float shader[kShCoefficients] = { 1.0f / (2.0f * sqrtPi), -sqrt3 / (2.0f * sqrtPi),
        sqrt3 / (2.0f * sqrtPi), -sqrt3 / (2.0f * sqrtPi), sqrt15 / (2.0f * sqrtPi),
        -sqrt15 / (2.0f * sqrtPi), sqrt5 / (4.0f * sqrtPi), -sqrt15 / (2.0f * sqrtPi),
        sqrt15 / (4.0f * sqrtPi) };
    for (unsigned i = 0; i < kShCoefficients; ++i)
    {
        const float factor = scale[i] * shader[i] * 0.318309886183791f;
        sh[i][0] *= factor;
        sh[i][1] *= factor;
        sh[i][2] *= factor;
    }
}

inline void evaluateIrradianceSh(const float (*sh)[3], const float* n, float* rgb)
{
    for (unsigned c = 0; c < 3; ++c)
    {
        float v = sh[0][c];
        v += sh[1][c] * n[1] + sh[2][c] * n[2] + sh[3][c] * n[0];
        v += sh[4][c] * (n[1] * n[0]) + sh[5][c] * (n[1] * n[2]) +
             sh[6][c] * (3.0f * n[2] * n[2] - 1.0f) + sh[7][c] * (n[2] * n[0]) +
             sh[8][c] * (n[0] * n[0] - n[1] * n[1]);
        rgb[c] = v > 0.0f ? v : 0.0f;
    }
}

} // namespace ibl

} // namespace zenapp
