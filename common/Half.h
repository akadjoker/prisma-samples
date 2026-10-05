#pragma once

#include <stdint.h>
#include <string.h>

namespace zenapp
{

namespace ibl
{

inline uint16_t floatToHalf(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0)
    {
        if (exponent < -10) return static_cast<uint16_t>(sign);
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1u) ++half;
        return static_cast<uint16_t>(sign | half);
    }
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u) ++half;
    return static_cast<uint16_t>(half);
}

inline float halfToFloat(uint16_t half)
{
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
    uint32_t exponent = (half >> 10) & 0x1Fu;
    uint32_t mantissa = half & 0x3FFu;
    uint32_t bits;
    if (exponent == 0)
    {
        if (mantissa == 0)
            bits = sign;
        else
        {
            exponent = 127 - 15 + 1;
            while (!(mantissa & 0x400u))
            {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 31)
        bits = sign | 0x7F800000u | (mantissa << 13);
    else
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace ibl

} // namespace zenapp
