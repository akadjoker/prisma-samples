#pragma once

#include "prisma/rhi/Types.h"

#include <cstddef>
#include <cstdint>

namespace prisma
{

enum class FormatFamily : std::uint8_t
{
    Plain,
    BC,
    ETC2,
    ASTC
};

struct FormatBlock
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t bytes;
};

inline FormatFamily formatFamily(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::BC1:
        case TextureFormat::BC1Srgb:
        case TextureFormat::BC2:
        case TextureFormat::BC2Srgb:
        case TextureFormat::BC3:
        case TextureFormat::BC3Srgb:
        case TextureFormat::BC4:
        case TextureFormat::BC5:
        case TextureFormat::BC6H:
        case TextureFormat::BC7:
        case TextureFormat::BC7Srgb:
            return FormatFamily::BC;
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGB8Srgb:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ETC2RGBA8Srgb:
        case TextureFormat::EACR11:
        case TextureFormat::EACRG11:
            return FormatFamily::ETC2;
        case TextureFormat::ASTC4x4:
        case TextureFormat::ASTC4x4Srgb:
        case TextureFormat::ASTC6x6:
        case TextureFormat::ASTC6x6Srgb:
        case TextureFormat::ASTC8x8:
        case TextureFormat::ASTC8x8Srgb:
            return FormatFamily::ASTC;
        default:
            return FormatFamily::Plain;
    }
}

inline bool isCompressedFormat(TextureFormat format)
{
    return formatFamily(format) != FormatFamily::Plain;
}

inline bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::Depth32F || format == TextureFormat::Depth24Stencil8;
}

inline FormatBlock formatBlock(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8:
            return { 1, 1, 1 };
        case TextureFormat::RG8:
            return { 1, 1, 2 };
        case TextureFormat::RGBA16F:
        case TextureFormat::RG32F:
            return { 1, 1, 8 };
        case TextureFormat::R16F:
            return { 1, 1, 2 };
        case TextureFormat::RGBA32F:
            return { 1, 1, 16 };
        case TextureFormat::BC1:
        case TextureFormat::BC1Srgb:
        case TextureFormat::BC4:
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGB8Srgb:
        case TextureFormat::EACR11:
            return { 4, 4, 8 };
        case TextureFormat::BC2:
        case TextureFormat::BC2Srgb:
        case TextureFormat::BC3:
        case TextureFormat::BC3Srgb:
        case TextureFormat::BC5:
        case TextureFormat::BC6H:
        case TextureFormat::BC7:
        case TextureFormat::BC7Srgb:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ETC2RGBA8Srgb:
        case TextureFormat::EACRG11:
        case TextureFormat::ASTC4x4:
        case TextureFormat::ASTC4x4Srgb:
            return { 4, 4, 16 };
        case TextureFormat::ASTC6x6:
        case TextureFormat::ASTC6x6Srgb:
            return { 6, 6, 16 };
        case TextureFormat::ASTC8x8:
        case TextureFormat::ASTC8x8Srgb:
            return { 8, 8, 16 };
        default:
            return { 1, 1, 4 };
    }
}

inline std::size_t levelBytes(TextureFormat format, std::uint32_t width, std::uint32_t height)
{
    const FormatBlock block = formatBlock(format);
    const std::size_t columns = (static_cast<std::size_t>(width) + block.width - 1) / block.width;
    const std::size_t rows = (static_cast<std::size_t>(height) + block.height - 1) / block.height;
    return columns * rows * block.bytes;
}

inline bool validRegion(TextureFormat format, std::uint32_t levelWidth, std::uint32_t levelHeight,
        const TextureRegion& region)
{
    const FormatBlock block = formatBlock(format);
    const std::uint64_t right = static_cast<std::uint64_t>(region.x) + region.width;
    const std::uint64_t bottom = static_cast<std::uint64_t>(region.y) + region.height;
    if (region.width == 0 || region.height == 0 || right > levelWidth || bottom > levelHeight)
        return false;
    if (region.x % block.width != 0 || region.y % block.height != 0) return false;
    if (region.width % block.width != 0 && right != levelWidth) return false;
    if (region.height % block.height != 0 && bottom != levelHeight) return false;
    return true;
}

inline bool isAdjacency(Topology topology)
{
    return topology == Topology::LinesAdjacency || topology == Topology::LineStripAdjacency ||
           topology == Topology::TrianglesAdjacency || topology == Topology::TriangleStripAdjacency;
}

inline bool isFloatFormat(TextureFormat format)
{
    return format == TextureFormat::RGBA16F || format == TextureFormat::R11G11B10F ||
           format == TextureFormat::R16F || format == TextureFormat::RG16F ||
           format == TextureFormat::R32F || format == TextureFormat::RG32F ||
           format == TextureFormat::RGBA32F;
}

inline bool isStorageFormat(TextureFormat format, bool gles)
{
    if (format == TextureFormat::RGBA8 || format == TextureFormat::RGBA16F ||
            format == TextureFormat::RGBA32F || format == TextureFormat::R32F ||
            format == TextureFormat::R32UInt)
        return true;
    if (gles) return false;
    return format == TextureFormat::R8 || format == TextureFormat::RG8 ||
           format == TextureFormat::RGB10A2 || format == TextureFormat::R11G11B10F ||
           format == TextureFormat::R16F || format == TextureFormat::RG16F ||
           format == TextureFormat::RG32F;
}

inline bool validIndirect(bool valid, std::uint32_t bufferSize, std::uint32_t offset,
        std::uint32_t drawCount, std::uint32_t stride, std::uint32_t commandSize)
{
    const std::uint32_t step = stride ? stride : commandSize;
    return valid && drawCount > 0 && step >= commandSize && step % 4 == 0 && offset % 4 == 0 &&
           static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(drawCount - 1) * step +
                           commandSize <=
                   bufferSize;
}

inline TextureRegion sourceRegion(const TextureCopy& copy)
{
    TextureRegion region;
    region.mip = copy.sourceMip;
    region.layer = copy.sourceLayer;
    region.x = copy.sourceX;
    region.y = copy.sourceY;
    region.width = copy.width;
    region.height = copy.height;
    return region;
}

inline TextureRegion destinationRegion(const TextureCopy& copy)
{
    TextureRegion region;
    region.mip = copy.destinationMip;
    region.layer = copy.destinationLayer;
    region.x = copy.destinationX;
    region.y = copy.destinationY;
    region.width = copy.width;
    region.height = copy.height;
    return region;
}

template<typename Texture>
bool validCopy(const Texture* source, const Texture* destination, const TextureCopy& copy,
        std::uint32_t sourceLayers, std::uint32_t destinationLayers)
{
    if (!source || !destination || source->format != destination->format || source->samples != 1 ||
            destination->samples != 1)
        return false;
    if (copy.sourceMip >= source->mipLevels || copy.destinationMip >= destination->mipLevels ||
            copy.sourceLayer >= sourceLayers || copy.destinationLayer >= destinationLayers)
        return false;
    if (copy.source == copy.destination && copy.sourceMip == copy.destinationMip &&
            copy.sourceLayer == copy.destinationLayer)
        return false;
    const std::uint32_t sourceWidth = source->width >> copy.sourceMip;
    const std::uint32_t sourceHeight = source->height >> copy.sourceMip;
    const std::uint32_t destinationWidth = destination->width >> copy.destinationMip;
    const std::uint32_t destinationHeight = destination->height >> copy.destinationMip;
    return validRegion(source->format, sourceWidth ? sourceWidth : 1,
                   sourceHeight ? sourceHeight : 1, sourceRegion(copy)) &&
           validRegion(destination->format, destinationWidth ? destinationWidth : 1,
                   destinationHeight ? destinationHeight : 1, destinationRegion(copy));
}

inline bool validBufferCopy(bool valid, bool sameBuffer, bool sourceIndex, bool destinationIndex,
        std::uint32_t sourceSize, std::uint32_t sourceOffset, std::uint32_t destinationSize,
        std::uint32_t destinationOffset, std::uint32_t size)
{
    return valid && !sameBuffer && sourceIndex == destinationIndex && size > 0 &&
           static_cast<std::uint64_t>(sourceOffset) + size <= sourceSize &&
           static_cast<std::uint64_t>(destinationOffset) + size <= destinationSize;
}

} // namespace prisma
