#pragma once

#include "Media.h"
#include "third_party/stb_image.h"

#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

#include <string.h>

namespace zenapp
{

struct PbrImage
{
    unsigned width = 0;
    unsigned height = 0;
    ct::Vector<unsigned char> rgba;
};

struct PbrTextureSet
{
    prisma::TextureHandle base;
    prisma::TextureHandle normal;
    prisma::TextureHandle orm;
};

inline bool loadPbrImage(const char* path, unsigned divisor, PbrImage* out)
{
    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes)) return false;
    int width = 0;
    int height = 0;
    int components = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
            &width, &height, &components, 4);
    if (!pixels) return false;
    const unsigned w = static_cast<unsigned>(width) / divisor > 0 ? static_cast<unsigned>(width) / divisor : 1;
    const unsigned h = static_cast<unsigned>(height) / divisor > 0 ? static_cast<unsigned>(height) / divisor : 1;
    out->width = w;
    out->height = h;
    out->rgba.resize(static_cast<size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
    {
        for (unsigned x = 0; x < w; ++x)
        {
            unsigned sum[4] = { 0, 0, 0, 0 };
            for (unsigned dy = 0; dy < divisor; ++dy)
            {
                for (unsigned dx = 0; dx < divisor; ++dx)
                {
                    const unsigned char* texel = pixels + (static_cast<size_t>(y * divisor + dy) *
                                                                   static_cast<unsigned>(width) +
                                                           x * divisor + dx) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += texel[c];
                }
            }
            unsigned char* target = out->rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c)
                target[c] = static_cast<unsigned char>(sum[c] / (divisor * divisor));
        }
    }
    stbi_image_free(pixels);
    return true;
}

inline prisma::TextureHandle createPbrTexture(prisma::Driver* driver, const PbrImage& image,
        bool srgb, const char* name)
{
    prisma::TextureDesc desc;
    desc.format = srgb ? prisma::TextureFormat::RGBA8Srgb : prisma::TextureFormat::RGBA8;
    desc.width = image.width;
    desc.height = image.height;
    desc.data = image.rgba.data();
    desc.mipLevels = 0;
    desc.generateMipmaps = true;
    desc.debugName = name;
    return driver->createTexture(desc);
}

inline void packChannel(const PbrImage& source, unsigned channelOut, PbrImage* target)
{
    for (unsigned y = 0; y < target->height; ++y)
    {
        const unsigned sy = y * source.height / target->height;
        for (unsigned x = 0; x < target->width; ++x)
        {
            const unsigned sx = x * source.width / target->width;
            target->rgba[(static_cast<size_t>(y) * target->width + x) * 4 + channelOut] =
                    source.rgba[(static_cast<size_t>(sy) * source.width + sx) * 4];
        }
    }
}

inline bool loadPbrSet(prisma::Driver* driver, const char* color, const char* normal,
        const char* ao, const char* roughness, const char* metallic, unsigned divisor,
        PbrTextureSet* out)
{
    *out = PbrTextureSet();
    PbrImage image;
    if (color && color[0])
    {
        if (!loadPbrImage(color, divisor, &image)) return false;
        out->base = createPbrTexture(driver, image, true, color);
    }
    if (normal && normal[0])
    {
        if (!loadPbrImage(normal, divisor, &image)) return false;
        out->normal = createPbrTexture(driver, image, false, normal);
    }
    const bool any = (ao && ao[0]) || (roughness && roughness[0]) || (metallic && metallic[0]);
    if (any)
    {
        PbrImage packed;
        PbrImage first;
        const char* paths[3] = { ao, roughness, metallic };
        const unsigned channels[3] = { 0, 1, 2 };
        bool sized = false;
        PbrImage maps[3];
        bool present[3] = { false, false, false };
        for (int i = 0; i < 3; ++i)
        {
            if (!paths[i] || !paths[i][0]) continue;
            if (!loadPbrImage(paths[i], divisor, &maps[i])) return false;
            present[i] = true;
            if (!sized)
            {
                packed.width = maps[i].width;
                packed.height = maps[i].height;
                sized = true;
            }
        }
        packed.rgba.resize(static_cast<size_t>(packed.width) * packed.height * 4);
        memset(packed.rgba.data(), 255, packed.rgba.size());
        for (int i = 0; i < 3; ++i)
            if (present[i]) packChannel(maps[i], channels[i], &packed);
        out->orm = createPbrTexture(driver, packed, false, "orm");
    }
    return true;
}

} // namespace zenapp
