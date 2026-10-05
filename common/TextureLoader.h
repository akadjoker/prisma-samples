#pragma once

#include "Bcn.h"
#include "Media.h"
#include "prisma/rhi/Driver.h"
#include "third_party/dds.h"
#include "third_party/stb_image.h"

#include <ct/vector.hpp>

#include <stdint.h>
#include <string.h>

namespace zenapp
{

struct DdsInfo
{
    prisma::TextureType type = prisma::TextureType::Texture2D;
    prisma::TextureFormat format = prisma::TextureFormat::RGBA8;
    unsigned width = 0;
    unsigned height = 0;
    unsigned depth = 1;
    unsigned mipLevels = 1;
    unsigned layers = 1;
    bool swapRedBlue = false;
    bool opaqueAlpha = false;
};

inline bool mapDxgiFormat(dds::DXGI_FORMAT format, bool srgb, DdsInfo* info)
{
    using prisma::TextureFormat;
    switch (format)
    {
        case dds::DXGI_FORMAT_R8G8B8A8_UNORM:
            info->format = srgb ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
            return true;
        case dds::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            info->format = TextureFormat::RGBA8Srgb;
            return true;
        case dds::DXGI_FORMAT_B8G8R8A8_UNORM:
        case dds::DXGI_FORMAT_B8G8R8X8_UNORM:
            info->format = srgb ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
            info->swapRedBlue = true;
            info->opaqueAlpha = format == dds::DXGI_FORMAT_B8G8R8X8_UNORM;
            return true;
        case dds::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            info->format = TextureFormat::RGBA8Srgb;
            info->swapRedBlue = true;
            return true;
        case dds::DXGI_FORMAT_R8_UNORM:
            info->format = TextureFormat::R8;
            return true;
        case dds::DXGI_FORMAT_R8G8_UNORM:
            info->format = TextureFormat::RG8;
            return true;
        case dds::DXGI_FORMAT_R16_FLOAT:
            info->format = TextureFormat::R16F;
            return true;
        case dds::DXGI_FORMAT_R16G16_FLOAT:
            info->format = TextureFormat::RG16F;
            return true;
        case dds::DXGI_FORMAT_R16G16B16A16_FLOAT:
            info->format = TextureFormat::RGBA16F;
            return true;
        case dds::DXGI_FORMAT_R32_FLOAT:
            info->format = TextureFormat::R32F;
            return true;
        case dds::DXGI_FORMAT_R32G32_FLOAT:
            info->format = TextureFormat::RG32F;
            return true;
        case dds::DXGI_FORMAT_R32G32B32A32_FLOAT:
            info->format = TextureFormat::RGBA32F;
            return true;
        case dds::DXGI_FORMAT_R32_UINT:
            info->format = TextureFormat::R32UInt;
            return true;
        case dds::DXGI_FORMAT_R10G10B10A2_UNORM:
            info->format = TextureFormat::RGB10A2;
            return true;
        case dds::DXGI_FORMAT_R11G11B10_FLOAT:
            info->format = TextureFormat::R11G11B10F;
            return true;
        case dds::DXGI_FORMAT_BC1_UNORM:
            info->format = srgb ? TextureFormat::BC1Srgb : TextureFormat::BC1;
            return true;
        case dds::DXGI_FORMAT_BC1_UNORM_SRGB:
            info->format = TextureFormat::BC1Srgb;
            return true;
        case dds::DXGI_FORMAT_BC2_UNORM:
            info->format = srgb ? TextureFormat::BC2Srgb : TextureFormat::BC2;
            return true;
        case dds::DXGI_FORMAT_BC2_UNORM_SRGB:
            info->format = TextureFormat::BC2Srgb;
            return true;
        case dds::DXGI_FORMAT_BC3_UNORM:
            info->format = srgb ? TextureFormat::BC3Srgb : TextureFormat::BC3;
            return true;
        case dds::DXGI_FORMAT_BC3_UNORM_SRGB:
            info->format = TextureFormat::BC3Srgb;
            return true;
        case dds::DXGI_FORMAT_BC4_UNORM:
            info->format = TextureFormat::BC4;
            return true;
        case dds::DXGI_FORMAT_BC5_UNORM:
            info->format = TextureFormat::BC5;
            return true;
        case dds::DXGI_FORMAT_BC6H_UF16:
            info->format = TextureFormat::BC6H;
            return true;
        case dds::DXGI_FORMAT_BC7_UNORM:
            info->format = srgb ? TextureFormat::BC7Srgb : TextureFormat::BC7;
            return true;
        case dds::DXGI_FORMAT_BC7_UNORM_SRGB:
            info->format = TextureFormat::BC7Srgb;
            return true;
        default:
            return false;
    }
}

inline bool isBlockCompressed(prisma::TextureFormat format)
{
    return format >= prisma::TextureFormat::BC1 && format <= prisma::TextureFormat::BC7Srgb;
}

inline bool parseDds(const unsigned char* data, size_t size, bool srgb, DdsInfo* info)
{
    const dds::Header header = dds::read_header(data, size);
    if (!header.is_valid()) return false;
    *info = DdsInfo();
    if (!mapDxgiFormat(header.format(), srgb, info)) return false;

    info->width = header.width();
    info->height = header.height();
    info->depth = header.depth();
    info->mipLevels = header.mip_levels();
    info->layers = header.array_size();
    if (header.is_3d())
    {
        info->type = prisma::TextureType::Texture3D;
        info->layers = 1;
    }
    else
    {
        info->depth = 1;
        if (header.is_cubemap())
        {
            info->type = info->layers > 6 ? prisma::TextureType::TextureCubeArray
                                          : prisma::TextureType::TextureCube;
            if (info->layers % 6 != 0) return false;
        }
        else if (info->layers > 1)
            info->type = prisma::TextureType::Texture2DArray;
    }
    const unsigned last = info->mipLevels - 1;
    const unsigned long long end =
            header.mip_offset(last, info->layers - 1) + header.mip_size(last);
    return end <= size;
}

inline prisma::TextureHandle createTextureFromDds(prisma::Driver* driver, const unsigned char* data,
        size_t size, bool srgb, bool generateMips, const char* name = nullptr,
        unsigned skipMips = 0)
{
    DdsInfo info;
    if (!parseDds(data, size, srgb, &info)) return prisma::TextureHandle();
    const prisma::Caps& caps = driver->caps();
    const prisma::TextureFormat format = info.format;
    if (isBlockCompressed(format) && !caps.textureBC) return prisma::TextureHandle();
    if (info.type == prisma::TextureType::TextureCubeArray && !caps.cubeArrays)
        return prisma::TextureHandle();

    const dds::Header header = dds::read_header(data, size);

    const bool isBc1 = format == prisma::TextureFormat::BC1 || format == prisma::TextureFormat::BC1Srgb;
    const bool isBc2 = format == prisma::TextureFormat::BC2 || format == prisma::TextureFormat::BC2Srgb;
    const bool isBc3 = format == prisma::TextureFormat::BC3 || format == prisma::TextureFormat::BC3Srgb;
    if (generateMips && info.mipLevels == 1 && info.layers == 1 &&
            info.type == prisma::TextureType::Texture2D && (isBc1 || isBc2 || isBc3))
    {
        const bool srgbFormat = format == prisma::TextureFormat::BC1Srgb ||
                                format == prisma::TextureFormat::BC2Srgb ||
                                format == prisma::TextureFormat::BC3Srgb;
        ct::Vector<unsigned char> pixels;
        pixels.resize(static_cast<size_t>(info.width) * info.height * 4);
        if (decodeBcn(isBc1 ? BcnKind::BC1 : isBc2 ? BcnKind::BC2 : BcnKind::BC3,
                    data + header.mip_offset(0, 0), static_cast<size_t>(header.mip_size(0)),
                    info.width, info.height, pixels.data()))
        {
            prisma::TextureDesc decoded;
            decoded.format = srgbFormat ? prisma::TextureFormat::RGBA8Srgb
                                        : prisma::TextureFormat::RGBA8;
            decoded.width = info.width;
            decoded.height = info.height;
            decoded.mipLevels = 0;
            decoded.generateMipmaps = true;
            decoded.data = pixels.data();
            decoded.debugName = name;
            return driver->createTexture(decoded);
        }
    }

    prisma::TextureDesc desc;
    const unsigned skip = info.type == prisma::TextureType::Texture2D && info.mipLevels > 1
                                  ? (skipMips < info.mipLevels - 1 ? skipMips : info.mipLevels - 1)
                                  : 0;
    desc.type = info.type;
    desc.format = format;
    desc.width = info.width >> skip > 0 ? info.width >> skip : 1;
    desc.height = info.height >> skip > 0 ? info.height >> skip : 1;
    desc.depth = info.type == prisma::TextureType::Texture3D          ? info.depth
                 : info.type == prisma::TextureType::TextureCubeArray ? info.layers / 6
                                                                      : info.layers;
    desc.mipLevels = info.mipLevels - skip;
    desc.debugName = name;

    const bool autoMips = generateMips && info.mipLevels == 1 && !isBlockCompressed(format) &&
                          info.type == prisma::TextureType::Texture2D &&
                          format != prisma::TextureFormat::R32UInt;
    if (autoMips)
    {
        desc.mipLevels = 0;
        desc.generateMipmaps = true;
        desc.data = data + header.mip_offset(0, 0);
        ct::Vector<unsigned char> swapped;
        if (info.swapRedBlue)
        {
            const size_t bytes = static_cast<size_t>(header.mip_size(0));
            swapped.resize(bytes);
            for (size_t i = 0; i + 3 < bytes; i += 4)
            {
                swapped[i] = static_cast<const unsigned char*>(desc.data)[i + 2];
                swapped[i + 1] = static_cast<const unsigned char*>(desc.data)[i + 1];
                swapped[i + 2] = static_cast<const unsigned char*>(desc.data)[i];
                swapped[i + 3] = info.opaqueAlpha
                                         ? 255
                                         : static_cast<const unsigned char*>(desc.data)[i + 3];
            }
            desc.data = swapped.data();
        }
        return driver->createTexture(desc);
    }

    const prisma::TextureHandle texture = driver->createTexture(desc);
    if (!texture.valid()) return texture;
    ct::Vector<unsigned char> converted;
    for (unsigned layer = 0; layer < info.layers; ++layer)
    {
        for (unsigned mip = 0; mip < info.mipLevels - skip; ++mip)
        {
            const unsigned char* source = data + header.mip_offset(mip + skip, layer);
            if (info.swapRedBlue)
            {
                const size_t bytes = static_cast<size_t>(header.mip_size(mip + skip));
                converted.resize(bytes);
                for (size_t i = 0; i + 3 < bytes; i += 4)
                {
                    converted[i] = source[i + 2];
                    converted[i + 1] = source[i + 1];
                    converted[i + 2] = source[i];
                    converted[i + 3] = info.opaqueAlpha ? 255 : source[i + 3];
                }
                source = converted.data();
            }
            driver->updateTexture(texture, mip, layer, source);
        }
    }
    return texture;
}

inline bool endsWith(const char* text, const char* suffix)
{
    const size_t length = strlen(text);
    const size_t tail = strlen(suffix);
    if (tail > length) return false;
    for (size_t i = 0; i < tail; ++i)
    {
        char a = text[length - tail + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return true;
}

inline prisma::TextureHandle createTextureFromImage(prisma::Driver* driver,
        const unsigned char* data, size_t size, bool srgb, bool generateMips, bool hdr,
        const char* name = nullptr)
{
    int width = 0;
    int height = 0;
    int components = 0;
    prisma::TextureDesc desc;
    desc.debugName = name;
    unsigned char* pixels = nullptr;
    float* floats = nullptr;
    if (hdr)
    {
        floats = stbi_loadf_from_memory(data, static_cast<int>(size), &width, &height, &components,
                4);
        desc.format = prisma::TextureFormat::RGBA32F;
        desc.data = floats;
    }
    else
    {
        pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &components,
                4);
        desc.format = srgb ? prisma::TextureFormat::RGBA8Srgb : prisma::TextureFormat::RGBA8;
        desc.data = pixels;
        desc.mipLevels = generateMips ? 0 : 1;
        desc.generateMipmaps = generateMips;
    }
    if (!desc.data) return prisma::TextureHandle();
    desc.width = static_cast<unsigned>(width);
    desc.height = static_cast<unsigned>(height);
    const prisma::TextureHandle texture = driver->createTexture(desc);
    stbi_image_free(pixels);
    stbi_image_free(floats);
    return texture;
}

inline prisma::TextureHandle loadTexture(prisma::Driver* driver, const char* path, bool srgb,
        bool generateMips, unsigned skipMips = 0)
{
    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes)) return prisma::TextureHandle();
    if (endsWith(path, ".dds"))
        return createTextureFromDds(driver, bytes.data(), bytes.size(), srgb, generateMips, path,
                skipMips);
    return createTextureFromImage(driver, bytes.data(), bytes.size(), srgb, generateMips,
            endsWith(path, ".hdr"), path);
}

} // namespace zenapp
