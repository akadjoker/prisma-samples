#pragma once

#include "Environment.h"
#include "Media.h"
#include "third_party/stb_image.h"

#include <ct/vector.hpp>

#include <math.h>

namespace zenapp
{

struct EquirectImage
{
    unsigned width = 0;
    unsigned height = 0;
    ct::Vector<float> rgb;
};

inline void equirectCoordinates(const float* direction, float* u, float* v)
{
    const float pi = 3.14159265358979f;
    const float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                               direction[2] * direction[2]);
    *u = (atan2f(direction[0], direction[2]) / pi + 1.0f) * 0.5f;
    *v = (1.0f - asinf(direction[1] / length) * 2.0f / pi) * 0.5f;
}

inline void sampleEquirect(const EquirectImage& image, const float* direction, float* out)
{
    float u, v;
    equirectCoordinates(direction, &u, &v);
    const float x = u * static_cast<float>(image.width) - 0.5f;
    const float y = v * static_cast<float>(image.height) - 0.5f;
    const float fx = floorf(x);
    const float fy = floorf(y);
    const float tx = x - fx;
    const float ty = y - fy;
    const int w = static_cast<int>(image.width);
    const int h = static_cast<int>(image.height);
    const int x0 = ((static_cast<int>(fx) % w) + w) % w;
    const int x1 = (x0 + 1) % w;
    int y0 = static_cast<int>(fy);
    int y1 = y0 + 1;
    y0 = y0 < 0 ? 0 : (y0 > h - 1 ? h - 1 : y0);
    y1 = y1 < 0 ? 0 : (y1 > h - 1 ? h - 1 : y1);
    for (int c = 0; c < 3; ++c)
    {
        const float* row0 = image.rgb.data() + static_cast<size_t>(y0) * w * 3;
        const float* row1 = image.rgb.data() + static_cast<size_t>(y1) * w * 3;
        const float top = row0[x0 * 3 + c] * (1.0f - tx) + row0[x1 * 3 + c] * tx;
        const float bottom = row1[x0 * 3 + c] * (1.0f - tx) + row1[x1 * 3 + c] * tx;
        float value = top * (1.0f - ty) + bottom * ty;
        value = value < 0.0f ? 0.0f : (value > 65504.0f ? 65504.0f : value);
        out[c] += value;
    }
}

inline bool equirectToFaces(const EquirectImage& image, unsigned faceSize, EnvironmentFaces* out)
{
    if (faceSize == 0 || image.width == 0 || image.width != image.height * 2) return false;
    if (image.rgb.size() != static_cast<size_t>(image.width) * image.height * 3) return false;

    unsigned samples = (image.width / 4 + faceSize / 2) / faceSize;
    samples = samples < 1 ? 1 : samples;
    out->size = faceSize;
    out->data.resize(static_cast<size_t>(faceSize) * faceSize * 4 * 6);
    for (unsigned face = 0; face < 6; ++face)
    {
        float* target = out->data.data() + static_cast<size_t>(face) * faceSize * faceSize * 4;
        for (unsigned py = 0; py < faceSize; ++py)
        {
            for (unsigned px = 0; px < faceSize; ++px)
            {
                float sum[3] = { 0.0f, 0.0f, 0.0f };
                for (unsigned sy = 0; sy < samples; ++sy)
                {
                    for (unsigned sx = 0; sx < samples; ++sx)
                    {
                        const float s = 2.0f * (static_cast<float>(px) +
                                                (static_cast<float>(sx) + 0.5f) / samples) /
                                                static_cast<float>(faceSize) -
                                        1.0f;
                        const float t = 1.0f - 2.0f * (static_cast<float>(py) +
                                                       (static_cast<float>(sy) + 0.5f) / samples) /
                                                       static_cast<float>(faceSize);
                        float direction[3];
                        switch (face)
                        {
                        case 0: direction[0] = 1.0f; direction[1] = t; direction[2] = -s; break;
                        case 1: direction[0] = -1.0f; direction[1] = t; direction[2] = s; break;
                        case 2: direction[0] = s; direction[1] = 1.0f; direction[2] = -t; break;
                        case 3: direction[0] = s; direction[1] = -1.0f; direction[2] = t; break;
                        case 4: direction[0] = s; direction[1] = t; direction[2] = 1.0f; break;
                        default: direction[0] = -s; direction[1] = t; direction[2] = -1.0f; break;
                        }
                        sampleEquirect(image, direction, sum);
                    }
                }
                const float scale = 1.0f / static_cast<float>(samples * samples);
                float* texel = target + (static_cast<size_t>(py) * faceSize + px) * 4;
                texel[0] = sum[0] * scale;
                texel[1] = sum[1] * scale;
                texel[2] = sum[2] * scale;
                texel[3] = 1.0f;
            }
        }
    }
    return true;
}

inline bool decodeEquirect(const unsigned char* bytes, size_t length, EquirectImage* out)
{
    int width = 0;
    int height = 0;
    int components = 0;
    float* pixels = stbi_loadf_from_memory(bytes, static_cast<int>(length), &width, &height,
            &components, 3);
    if (!pixels) return false;
    out->width = static_cast<unsigned>(width);
    out->height = static_cast<unsigned>(height);
    out->rgb.resize(static_cast<size_t>(width) * height * 3);
    memcpy(out->rgb.data(), pixels, out->rgb.size() * sizeof(float));
    stbi_image_free(pixels);
    return true;
}

inline bool loadEquirectFaces(const char* path, unsigned faceSize, EnvironmentFaces* out)
{
    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes)) return false;
    EquirectImage image;
    if (!decodeEquirect(bytes.data(), bytes.size(), &image)) return false;
    if (faceSize == 0)
    {
        faceSize = 16;
        while (faceSize * 2 <= image.width / 4) faceSize *= 2;
    }
    return equirectToFaces(image, faceSize, out);
}

} // namespace zenapp
