#pragma once

#include "prisma/rhi/Driver.h"

namespace zenapp
{

struct ColorPyramid
{
    enum
    {
        kLevels = 7
    };

    prisma::TextureHandle level[kLevels];
    prisma::TextureHandle temp[kLevels];
    unsigned width[kLevels] = {};
    unsigned height[kLevels] = {};
    prisma::SamplerHandle sampler;
    prisma::BufferHandle uniforms;
    unsigned stride = 0;
    prisma::PipelineHandle blur;

    bool valid() const;
};

bool createColorPyramid(prisma::Driver* driver, unsigned width, unsigned height, ColorPyramid* out);
void renderColorPyramid(prisma::Driver* driver, const ColorPyramid& pyramid);
void bindColorPyramid(prisma::Driver* driver, const ColorPyramid& pyramid, unsigned firstSlot);
void destroyColorPyramid(prisma::Driver* driver, ColorPyramid* pyramid);
float colorPyramidLodOffset(float verticalFovRadians, unsigned height);

} // namespace zenapp
