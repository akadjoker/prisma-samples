#pragma once

#include "GltfModel.h"
#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

namespace zenapp
{

struct GltfMaterialUniforms
{
    float baseColor[4];
    float surface[4];
    float specular[4];
    float emissive[4];
    float flags[4];
    float modes[4];
};

struct GltfGpuOptions
{
    unsigned skipMips = 0;
    bool preferDds = true;
    float anisotropy = 8.0f;
};

struct GltfGpu
{
    enum
    {
        kTextureSlotBase = 2,
        kTextureSlotCount = 5,
        kMaterialBlockSlot = 7
    };

    prisma::BufferHandle vertexBuffer;
    prisma::BufferHandle indexBuffer;
    prisma::BufferHandle materialBuffer;
    prisma::SamplerHandle repeatSampler;
    prisma::SamplerHandle clampSampler;
    prisma::TextureHandle fallback;
    ct::Vector<prisma::TextureHandle> textures;
    ct::Vector<bool> textureRepeat;
    unsigned materialStride = 0;
    unsigned texturesLoaded = 0;
    unsigned texturesFailed = 0;
};

bool createGltfGpu(prisma::Driver* driver, const GltfModel& model, const GltfGpuOptions& options,
        GltfGpu* out);
void destroyGltfGpu(prisma::Driver* driver, GltfGpu* gpu);
void bindGltfMaterial(prisma::Driver* driver, const GltfGpu& gpu, const GltfModel& model,
        int material);
void bindGltfGeometry(prisma::Driver* driver, const GltfGpu& gpu);
void drawGltfPrimitive(prisma::Driver* driver, const GltfGpu& gpu, const GltfPrimitive& primitive);

} // namespace zenapp
