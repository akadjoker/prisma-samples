#pragma once

#include "prisma/rhi/Driver.h"

namespace zenapp
{

struct PrefilterDesc
{
    unsigned size = 256;
    unsigned minSize = 16;
    unsigned sampleCount = 1024;
};

struct PrefilteredEnvironment
{
    prisma::TextureHandle texture;
    unsigned size = 0;
    unsigned levels = 0;
};

float lodToPerceptualRoughness(float lod);

bool prefilterEnvironment(prisma::Driver* driver, prisma::TextureHandle source,
        unsigned sourceSize, unsigned sourceLevels, const PrefilterDesc& desc,
        PrefilteredEnvironment* out);

} // namespace zenapp
