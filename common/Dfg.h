#pragma once

#include "Half.h"
#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace zenapp
{

namespace ibl
{

inline float saturate(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float pow5(float x)
{
    const float x2 = x * x;
    return x2 * x2 * x;
}

inline void hammersley(uint32_t i, float inverseCount, float* out)
{
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    out[0] = static_cast<float>(i) * inverseCount;
    out[1] = static_cast<float>(bits) * (0.5f / 2147483648.0f);
}

inline void importanceSampleGgx(const float* u, float a, float* h)
{
    const float phi = 2.0f * 3.14159265358979f * u[0];
    const float cosTheta2 = (1.0f - u[1]) / (1.0f + (a + 1.0f) * ((a - 1.0f) * u[1]));
    const float cosTheta = sqrtf(cosTheta2);
    const float sinTheta = sqrtf(1.0f - cosTheta2);
    h[0] = sinTheta * cosf(phi);
    h[1] = sinTheta * sinf(phi);
    h[2] = cosTheta;
}

inline float visibility(float noV, float noL, float a)
{
    const float a2 = a * a;
    const float ggxL = noV * sqrtf((noL - noL * a2) * noL + a2);
    const float ggxV = noL * sqrtf((noV - noV * a2) * noV + a2);
    return 0.5f / (ggxV + ggxL);
}

inline void dfvMultiscatter(float noV, float linearRoughness, unsigned sampleCount, float* out)
{
    float x = 0.0f;
    float y = 0.0f;
    const float v[3] = { sqrtf(1.0f - noV * noV), 0.0f, noV };
    const float inverseCount = 1.0f / static_cast<float>(sampleCount);
    for (unsigned i = 0; i < sampleCount; ++i)
    {
        float u[2];
        float h[3];
        hammersley(i, inverseCount, u);
        importanceSampleGgx(u, linearRoughness, h);
        const float vDotH = v[0] * h[0] + v[1] * h[1] + v[2] * h[2];
        const float l[3] = { 2.0f * vDotH * h[0] - v[0], 2.0f * vDotH * h[1] - v[1],
            2.0f * vDotH * h[2] - v[2] };
        const float vDotHClamped = saturate(vDotH);
        const float noL = saturate(l[2]);
        const float noH = saturate(h[2]);
        if (noL > 0.0f)
        {
            const float term = visibility(noV, noL, linearRoughness) * noL * (vDotHClamped / noH);
            x += term * pow5(1.0f - vDotHClamped);
            y += term;
        }
    }
    out[0] = x * 4.0f * inverseCount;
    out[1] = y * 4.0f * inverseCount;
}

inline void computeDfg(unsigned size, unsigned sampleCount, ct::Vector<float>* rg)
{
    rg->resize(static_cast<size_t>(size) * size * 2);
    for (unsigned row = 0; row < size; ++row)
    {
        const float perceptualRoughness = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);
        const float linearRoughness = perceptualRoughness * perceptualRoughness;
        for (unsigned column = 0; column < size; ++column)
        {
            const float noV = (static_cast<float>(column) + 0.5f) / static_cast<float>(size);
            dfvMultiscatter(noV, linearRoughness, sampleCount,
                    rg->data() + (static_cast<size_t>(row) * size + column) * 2);
        }
    }
}

inline prisma::TextureHandle createDfgTexture(prisma::Driver* driver, unsigned size = 64,
        unsigned sampleCount = 1024)
{
    ct::Vector<float> rg;
    computeDfg(size, sampleCount, &rg);
    ct::Vector<uint16_t> half;
    half.resize(rg.size());
    for (size_t i = 0; i < rg.size(); ++i) half[i] = floatToHalf(rg[i]);

    prisma::TextureDesc desc;
    desc.format = prisma::TextureFormat::RG16F;
    desc.width = size;
    desc.height = size;
    desc.data = half.data();
    desc.debugName = "dfg";
    return driver->createTexture(desc);
}

} // namespace ibl

} // namespace zenapp
