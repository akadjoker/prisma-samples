#pragma once

#include <cstdint>

namespace prisma
{

struct Caps
{
    bool gles = false;
    std::uint32_t versionMajor = 0;
    std::uint32_t versionMinor = 0;
    bool compute = false;
    bool geometryShaders = false;
    bool tessellation = false;
    bool multipleWindows = false;
    bool indirectDraw = false;
    bool storageBuffersInGraphics = false;
    bool storageWritesInGraphics = false;
    bool debugOutput = false;
    bool floatColorTargets = false;
    bool floatLinearFiltering = false;
    bool wireframe = false;
    bool independentBlend = false;
    bool textureBC = false;
    bool textureETC2 = false;
    bool textureASTC = false;
    bool cubeArrays = false;
    bool compressedTextureCopy = false;
    bool occlusionQueries = false;
    bool timerQueries = false;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxColorTargets = 0;
    std::uint32_t maxSamples = 1;
    std::uint32_t maxPatchControlPoints = 0;
    std::uint32_t uniformBufferOffsetAlignment = 1;
    std::uint32_t storageBufferOffsetAlignment = 1;
    float maxAnisotropy = 1.0f;
};

} // namespace prisma
