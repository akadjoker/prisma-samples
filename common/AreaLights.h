#pragma once

#include "Half.h"
#include "LtcTables.h"
#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

namespace zenapp
{

// Matches AreaLight of ltc.glsl.
struct AreaLightData
{
    float corners[4][4];
    float colorIntensity[4];
    float flags[4];
};

// A rectangle centred on center whose sides are twice the vectors right and up; the light shines
// along right x up, the corners are counter clockwise seen from that side.
inline AreaLightData makeRectangleLight(const float* center, const float* right, const float* up,
        const float* color, float intensity, bool twoSided)
{
    AreaLightData light;
    const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const float sy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; ++i)
    {
        for (int c = 0; c < 3; ++c)
            light.corners[i][c] = center[c] + right[c] * sx[i] + up[c] * sy[i];
        light.corners[i][3] = 1.0f;
    }
    for (int c = 0; c < 3; ++c) light.colorIntensity[c] = color[c];
    light.colorIntensity[3] = intensity;
    light.flags[0] = twoSided ? 1.0f : 0.0f;
    light.flags[1] = light.flags[2] = light.flags[3] = 0.0f;
    return light;
}

// The table of inverse matrices for ltc.glsl, in float when the GPU filters float textures and in
// half float otherwise.
inline prisma::TextureHandle createLtcTexture(prisma::Driver* driver)
{
    prisma::TextureDesc desc;
    desc.width = ltc::kSize;
    desc.height = ltc::kSize;
    desc.debugName = "ltc inverse";
    if (driver->caps().floatLinearFiltering)
    {
        desc.format = prisma::TextureFormat::RGBA32F;
        desc.data = ltc::kInverse;
        return driver->createTexture(desc);
    }
    ct::Vector<uint16_t> half;
    half.resize(static_cast<size_t>(ltc::kSize) * ltc::kSize * 4);
    for (size_t i = 0; i < half.size(); ++i) half[i] = ibl::floatToHalf(ltc::kInverse[i]);
    desc.format = prisma::TextureFormat::RGBA16F;
    desc.data = half.data();
    return driver->createTexture(desc);
}

inline prisma::SamplerHandle createLtcSampler(prisma::Driver* driver)
{
    prisma::SamplerDesc desc;
    desc.mipFilter = prisma::MipFilter::None;
    desc.addressU = prisma::AddressMode::ClampToEdge;
    desc.addressV = prisma::AddressMode::ClampToEdge;
    desc.debugName = "ltc sampler";
    return driver->createSampler(desc);
}

} // namespace zenapp
