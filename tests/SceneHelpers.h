#pragma once

#include "Froxelizer.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace scene
{

inline uint32_t& seed()
{
    static uint32_t value = 987654321u;
    return value;
}

inline float random01()
{
    seed() = seed() * 1664525u + 1013904223u;
    return static_cast<float>(seed() >> 8) / 16777216.0f;
}

inline float randomRange(float low, float high)
{
    return low + (high - low) * random01();
}

inline void normalize(float* v)
{
    const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= length;
}

inline void perspective(float fovY, float aspect, float nearPlane, float farPlane, float* m)
{
    memset(m, 0, 16 * sizeof(float));
    const float t = tanf(fovY * 0.5f);
    m[0] = 1.0f / (aspect * t);
    m[5] = 1.0f / t;
    m[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    m[11] = -1.0f;
    m[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
}

inline void lookAt(const float* eye, const float* target, float* m)
{
    float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    normalize(f);
    float s[3] = { -f[2], 0.0f, f[0] };
    normalize(s);
    const float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
        s[0] * f[1] - s[1] * f[0] };
    memset(m, 0, 16 * sizeof(float));
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    m[15] = 1.0f;
}

struct Scene
{
    float eye[3];
    float view[16];
    float projection[16];
    unsigned width;
    unsigned height;
    float nearPlane;
    float farPlane;
    float zLightNear;
    float zLightFar;
};

inline void worldFromClip(const Scene& s, float ndcX, float ndcY, float depth, float* world)
{
    const float v[3] = { ndcX * depth / s.projection[0], ndcY * depth / s.projection[5], -depth };
    const float t[3] = { s.view[12], s.view[13], s.view[14] };
    for (int j = 0; j < 3; ++j)
    {
        world[j] = 0.0f;
        for (int i = 0; i < 3; ++i) world[j] += s.view[j * 4 + i] * (v[i] - t[i]);
    }
}

inline Scene makeScene(unsigned width, unsigned height)
{
    Scene s;
    s.width = width;
    s.height = height;
    s.nearPlane = 0.1f;
    s.farPlane = 100.0f;
    s.zLightNear = 2.0f;
    s.zLightFar = 80.0f;
    perspective(0.9f, static_cast<float>(width) / static_cast<float>(height), s.nearPlane,
            s.farPlane, s.projection);
    const float eye[3] = { 3.0f, 2.0f, 9.0f };
    const float target[3] = { -1.0f, 0.5f, -20.0f };
    memcpy(s.eye, eye, sizeof(eye));
    lookAt(eye, target, s.view);
    return s;
}

} // namespace scene
