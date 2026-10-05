#pragma once

#include "prisma/rhi/Driver.h"

#include <ct/function.hpp>

namespace zenapp
{

struct SunShadowParams
{
    float matrix[16];
    float params[4];
    float axisX[4];
    float axisY[4];
};

struct SunShadow
{
    prisma::TextureHandle map;
    prisma::SamplerHandle sampler;
    prisma::BufferHandle uniforms;
    prisma::BufferHandle lightFrame;
    prisma::PipelineHandle pipeline;
    bool drawn = false;

    bool valid() const;
};

typedef ct::Function<void(prisma::PipelineHandle)> SunShadowDraw;

bool createSunShadow(prisma::Driver* driver, unsigned size, const SunShadowParams& params,
        SunShadow* out);
void renderSunShadow(prisma::Driver* driver, SunShadow* shadow, const SunShadowDraw& drawCasters);
void bindSunShadow(prisma::Driver* driver, const SunShadow& shadow);
void destroySunShadow(prisma::Driver* driver, SunShadow* shadow);

} // namespace zenapp
