#pragma once

#include "prisma/rhi/Driver.h"

#include <ct/function.hpp>

namespace zenapp
{

struct SsaoDesc
{
    unsigned width = 0;
    unsigned height = 0;
    const float* projection = nullptr;
    const float* inverseProjection = nullptr;
    float farPlane = 0.0f;
    bool enabled = true;
    bool debug = false;
};

struct Ssao
{
    enum
    {
        kLevels = 5
    };

    prisma::TextureHandle depth[kLevels];
    prisma::TextureHandle ao[3];
    prisma::SamplerHandle depthSampler;
    prisma::SamplerHandle nearestSampler;
    prisma::SamplerHandle linearSampler;
    prisma::BufferHandle uniforms;
    unsigned stride = 0;
    unsigned paramsRange = 0;
    prisma::PipelineHandle structure;
    prisma::PipelineHandle structureDouble;
    prisma::PipelineHandle mip;
    prisma::PipelineHandle sao;
    prisma::PipelineHandle blur;
    bool enabled = true;

    bool valid() const;
};

typedef ct::Function<void(prisma::PipelineHandle, prisma::PipelineHandle)> SsaoDrawStructure;

bool createSsao(prisma::Driver* driver, const SsaoDesc& desc, Ssao* out);
void renderSsao(prisma::Driver* driver, const Ssao& ssao, const SsaoDrawStructure& drawStructure);
void bindSsao(prisma::Driver* driver, const Ssao& ssao);
void destroySsao(prisma::Driver* driver, Ssao* ssao);

} // namespace zenapp
