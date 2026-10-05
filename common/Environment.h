#pragma once

#include "Half.h"
#include "Sh.h"
#include "TextureLoader.h"

#include <ct/vector.hpp>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace zenapp
{

struct EnvironmentFaces
{
    unsigned size = 0;
    ct::Vector<float> data;

    const float* face(unsigned index) const
    {
        return data.data() + static_cast<size_t>(index) * size * size * 4;
    }
};

inline bool decodeEnvironmentFaces(const unsigned char* bytes, size_t length, EnvironmentFaces* out)
{
    DdsInfo info;
    if (!parseDds(bytes, length, false, &info)) return false;
    if (info.type != prisma::TextureType::TextureCube || info.width != info.height) return false;
    const bool half = info.format == prisma::TextureFormat::RGBA16F;
    if (!half && info.format != prisma::TextureFormat::RGBA32F) return false;

    const dds::Header header = dds::read_header(bytes, length);
    out->size = info.width;
    const size_t texels = static_cast<size_t>(info.width) * info.height;
    out->data.resize(texels * 4 * 6);
    for (unsigned face = 0; face < 6; ++face)
    {
        const unsigned char* source = bytes + header.mip_offset(0, face);
        float* target = out->data.data() + texels * 4 * face;
        if (half)
        {
            for (size_t i = 0; i < texels * 4; ++i)
            {
                uint16_t value;
                memcpy(&value, source + i * sizeof(value), sizeof(value));
                target[i] = ibl::halfToFloat(value);
            }
        }
        else
            memcpy(target, source, texels * 4 * sizeof(float));
    }
    return true;
}

inline bool loadEnvironmentFaces(const char* path, EnvironmentFaces* out)
{
    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes)) return false;
    return decodeEnvironmentFaces(bytes.data(), bytes.size(), out);
}

inline prisma::TextureHandle createEnvironmentCubemap(prisma::Driver* driver,
        const EnvironmentFaces& environment, const char* name = nullptr)
{
    const size_t perFace = static_cast<size_t>(environment.size) * environment.size * 4;
    ct::Vector<uint16_t> half;
    half.resize(perFace * 6);
    for (size_t i = 0; i < half.size(); ++i) half[i] = ibl::floatToHalf(environment.data[i]);

    prisma::TextureDesc desc;
    desc.type = prisma::TextureType::TextureCube;
    desc.format = prisma::TextureFormat::RGBA16F;
    desc.width = environment.size;
    desc.height = environment.size;
    desc.mipLevels = 0;
    desc.generateMipmaps = true;
    desc.data = half.data();
    desc.debugName = name;
    return driver->createTexture(desc);
}

inline unsigned mipLevelCount(unsigned size)
{
    unsigned levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

inline void computeEnvironmentSh(const EnvironmentFaces& environment, float (*sh)[3])
{
    const float* faces[6];
    for (unsigned i = 0; i < 6; ++i) faces[i] = environment.face(i);
    ibl::computeIrradianceSh(faces, environment.size, 4, sh);
}

} // namespace zenapp
