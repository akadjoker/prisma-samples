#pragma once

#include "Dfg.h"
#include "Environment.h"
#include "Prefilter.h"
#include "prisma/rhi/Driver.h"

namespace zenapp
{

struct IblUniforms
{
    float sh[ibl::kShCoefficients][4];
    float params[4];
};

struct Ibl
{
    prisma::TextureHandle environment;
    prisma::TextureHandle specular;
    prisma::TextureHandle dfg;
    prisma::BufferHandle uniforms;
    prisma::SamplerHandle cubeSampler;
    prisma::SamplerHandle dfgSampler;
    unsigned levels = 0;

    bool valid() const
    {
        return environment.valid() && specular.valid() && dfg.valid() && uniforms.valid() &&
               cubeSampler.valid() && dfgSampler.valid();
    }
};

inline bool createIbl(prisma::Driver* driver, const EnvironmentFaces& faces, Ibl* out,
        float luminance = 1.0f)
{
    out->environment = createEnvironmentCubemap(driver, faces, "environment");
    if (!out->environment.valid()) return false;

    PrefilterDesc desc;
    desc.size = faces.size < 256 ? faces.size : 256;
    PrefilteredEnvironment filtered;
    if (!prefilterEnvironment(driver, out->environment, faces.size, mipLevelCount(faces.size),
                desc, &filtered))
        return false;
    out->specular = filtered.texture;
    out->levels = filtered.levels;

    out->dfg = ibl::createDfgTexture(driver);

    float sh[ibl::kShCoefficients][3];
    computeEnvironmentSh(faces, sh);
    IblUniforms block;
    for (unsigned i = 0; i < ibl::kShCoefficients; ++i)
    {
        block.sh[i][0] = sh[i][0];
        block.sh[i][1] = sh[i][1];
        block.sh[i][2] = sh[i][2];
        block.sh[i][3] = 0.0f;
    }
    block.params[0] = static_cast<float>(filtered.levels - 1);
    block.params[1] = luminance;
    block.params[2] = block.params[3] = 0.0f;

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(block);
    bufferDesc.data = &block;
    bufferDesc.debugName = "ibl uniforms";
    out->uniforms = driver->createBuffer(bufferDesc);

    prisma::SamplerDesc cubeDesc;
    cubeDesc.addressU = prisma::AddressMode::ClampToEdge;
    cubeDesc.addressV = prisma::AddressMode::ClampToEdge;
    cubeDesc.addressW = prisma::AddressMode::ClampToEdge;
    cubeDesc.debugName = "ibl cube sampler";
    out->cubeSampler = driver->createSampler(cubeDesc);

    prisma::SamplerDesc dfgDesc;
    dfgDesc.mipFilter = prisma::MipFilter::None;
    dfgDesc.addressU = prisma::AddressMode::ClampToEdge;
    dfgDesc.addressV = prisma::AddressMode::ClampToEdge;
    dfgDesc.debugName = "ibl dfg sampler";
    out->dfgSampler = driver->createSampler(dfgDesc);
    return out->valid();
}

inline void bindIbl(prisma::Driver* driver, const Ibl& ibl)
{
    driver->bindUniformBuffer(1, ibl.uniforms, 0, sizeof(IblUniforms));
    driver->bindTexture(0, ibl.specular, ibl.cubeSampler);
    driver->bindTexture(1, ibl.dfg, ibl.dfgSampler);
}

inline void destroyIbl(prisma::Driver* driver, Ibl* ibl)
{
    driver->destroy(ibl->environment);
    driver->destroy(ibl->specular);
    driver->destroy(ibl->dfg);
    driver->destroy(ibl->uniforms);
    driver->destroy(ibl->cubeSampler);
    driver->destroy(ibl->dfgSampler);
    *ibl = Ibl();
}

} // namespace zenapp
