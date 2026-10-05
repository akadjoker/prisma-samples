#pragma once

#include "prisma/rhi/Driver.h"

namespace zenapp
{

struct PostProcess
{
    enum
    {
        kBloomLevels = 6,
        kBloomHeight = 384
    };

    prisma::SamplerHandle sampler;
    prisma::TextureHandle ldrColor;
    prisma::TextureHandle bloom[kBloomLevels];
    unsigned bloomWidth[kBloomLevels] = {};
    unsigned bloomHeight[kBloomLevels] = {};
    prisma::BufferHandle uniforms;
    unsigned stride = 0;
    prisma::PipelineHandle down2x;
    prisma::PipelineHandle down13;
    prisma::PipelineHandle down9;
    prisma::PipelineHandle up;
    prisma::PipelineHandle final;
    prisma::PipelineHandle fxaa;
    float bloomStrength = 0.0f;

    bool valid() const;
};

bool createPostProcess(prisma::Driver* driver, unsigned width, unsigned height,
        float bloomStrength, bool fxaa, PostProcess* out);
void renderPostProcess(prisma::Driver* driver, const PostProcess& post,
        prisma::TextureHandle scene, const prisma::RenderPassDesc& windowPass);
void destroyPostProcess(prisma::Driver* driver, PostProcess* post);

} // namespace zenapp
