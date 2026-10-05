#pragma once

#include <stdint.h>
#include <stddef.h>

namespace zenapp
{

enum class BcnKind
{
    BC1,
    BC2,
    BC3
};

namespace bcn
{

inline void expand565(uint16_t color, uint8_t* out)
{
    const unsigned r = (color >> 11) & 0x1F;
    const unsigned g = (color >> 5) & 0x3F;
    const unsigned b = color & 0x1F;
    out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
}

inline void colorBlock(const uint8_t* block, bool punchThrough, uint8_t texels[16][4])
{
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    const uint32_t bits = static_cast<uint32_t>(block[4]) | (static_cast<uint32_t>(block[5]) << 8) |
                          (static_cast<uint32_t>(block[6]) << 16) |
                          (static_cast<uint32_t>(block[7]) << 24);

    uint8_t palette[4][4];
    expand565(c0, palette[0]);
    expand565(c1, palette[1]);
    palette[0][3] = 255;
    palette[1][3] = 255;
    if (!punchThrough || c0 > c1)
    {
        for (int i = 0; i < 3; ++i)
        {
            palette[2][i] = static_cast<uint8_t>((2 * palette[0][i] + palette[1][i]) / 3);
            palette[3][i] = static_cast<uint8_t>((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = 255;
        palette[3][3] = 255;
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            palette[2][i] = static_cast<uint8_t>((palette[0][i] + palette[1][i]) / 2);
            palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }

    for (int i = 0; i < 16; ++i)
    {
        const uint32_t index = (bits >> (i * 2)) & 3u;
        for (int c = 0; c < 4; ++c) texels[i][c] = palette[index][c];
    }
}

inline void alphaExplicit(const uint8_t* block, uint8_t texels[16][4])
{
    for (int i = 0; i < 16; ++i)
    {
        const uint8_t nibble = (i & 1) ? static_cast<uint8_t>(block[i / 2] >> 4)
                                       : static_cast<uint8_t>(block[i / 2] & 0x0F);
        texels[i][3] = static_cast<uint8_t>((nibble << 4) | nibble);
    }
}

inline void alphaInterpolated(const uint8_t* block, uint8_t texels[16][4])
{
    uint8_t alpha[8];
    alpha[0] = block[0];
    alpha[1] = block[1];
    if (alpha[0] > alpha[1])
    {
        for (int i = 0; i < 6; ++i)
            alpha[2 + i] = static_cast<uint8_t>(((6 - i) * alpha[0] + (1 + i) * alpha[1]) / 7);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            alpha[2 + i] = static_cast<uint8_t>(((4 - i) * alpha[0] + (1 + i) * alpha[1]) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (i * 8);
    for (int i = 0; i < 16; ++i) texels[i][3] = alpha[(bits >> (i * 3)) & 7u];
}

} // namespace bcn

inline size_t bcnBlockBytes(BcnKind kind)
{
    return kind == BcnKind::BC1 ? 8u : 16u;
}

inline bool decodeBcn(BcnKind kind, const uint8_t* data, size_t size, unsigned width,
        unsigned height, uint8_t* rgba)
{
    const unsigned blocksX = (width + 3) / 4;
    const unsigned blocksY = (height + 3) / 4;
    const size_t blockBytes = bcnBlockBytes(kind);
    if (size < static_cast<size_t>(blocksX) * blocksY * blockBytes) return false;

    for (unsigned by = 0; by < blocksY; ++by)
    {
        for (unsigned bx = 0; bx < blocksX; ++bx)
        {
            const uint8_t* block = data + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
            uint8_t texels[16][4];
            if (kind == BcnKind::BC1)
                bcn::colorBlock(block, true, texels);
            else
            {
                bcn::colorBlock(block + 8, false, texels);
                if (kind == BcnKind::BC2)
                    bcn::alphaExplicit(block, texels);
                else
                    bcn::alphaInterpolated(block, texels);
            }
            for (unsigned row = 0; row < 4 && by * 4 + row < height; ++row)
            {
                for (unsigned column = 0; column < 4 && bx * 4 + column < width; ++column)
                {
                    uint8_t* out = rgba + (static_cast<size_t>(by * 4 + row) * width +
                                                  (bx * 4 + column)) * 4;
                    for (int c = 0; c < 4; ++c) out[c] = texels[row * 4 + column][c];
                }
            }
        }
    }
    return true;
}

} // namespace zenapp
