#pragma once

#include "Froxelizer.h"

#include <math.h>
#include <string.h>

namespace zenapp
{

struct LightData
{
    float positionFalloff[4];
    float colorIntensity[4];
    float direction[4];
    float spot[4];
};

struct ClusterUniforms
{
    float sunDirection[4];
    float sunColorIntensity[4];
    float cells[4];
    float counts[4];
};

struct ClusteredBuffers
{
    alignas(256) ClusterUniforms cluster;
    alignas(256) LightData lights[Froxelizer::kMaxLights + 1];
    alignas(256) uint32_t froxels[Froxelizer::kEntryCount];
    alignas(256) uint8_t records[Froxelizer::kRecordCount];
};

struct LightSet
{
    float sunDirection[4];
    float sunColorIntensity[4];
    LightData lights[Froxelizer::kMaxLights + 1];
    FroxelLight culling[Froxelizer::kMaxLights + 1];
    unsigned count;
};

inline void clearLights(LightSet* set)
{
    memset(set, 0, sizeof(*set));
}

inline void setSunLight(LightSet* set, const float* toLight, const float* color, float intensity)
{
    const float length = sqrtf(toLight[0] * toLight[0] + toLight[1] * toLight[1] +
                               toLight[2] * toLight[2]);
    for (int i = 0; i < 3; ++i)
    {
        set->sunDirection[i] = toLight[i] / length;
        set->sunColorIntensity[i] = color[i];
    }
    set->sunDirection[3] = 0.0f;
    set->sunColorIntensity[3] = intensity;
}

inline int addPointLight(LightSet* set, const float* position, const float* color,
        float intensity, float radius)
{
    if (set->count >= Froxelizer::kMaxLights) return -1;
    const unsigned index = set->count++;
    LightData& light = set->lights[index];
    FroxelLight& culling = set->culling[index];
    for (int i = 0; i < 3; ++i)
    {
        light.positionFalloff[i] = position[i];
        light.colorIntensity[i] = color[i];
        culling.position[i] = position[i];
    }
    light.positionFalloff[3] = radius > 0.0f ? 1.0f / (radius * radius) : 0.0f;
    light.colorIntensity[3] = intensity;
    light.spot[2] = 0.0f;
    culling.radius = radius;
    culling.spot = false;
    culling.outerAngle = 0.0f;
    return static_cast<int>(index);
}

inline int addSpotLight(LightSet* set, const float* position, const float* direction,
        const float* color, float intensity, float radius, float innerAngle, float outerAngle)
{
    const int index = addPointLight(set, position, color, intensity, radius);
    if (index < 0) return index;
    LightData& light = set->lights[index];
    FroxelLight& culling = set->culling[index];
    const float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                               direction[2] * direction[2]);
    for (int i = 0; i < 3; ++i)
    {
        light.direction[i] = direction[i] / length;
        culling.direction[i] = direction[i] / length;
    }
    const float halfPi = 1.57079632679f;
    const float minimum = 0.0087266463f;
    const float outerMagnitude = fabsf(outerAngle);
    const float innerMagnitude = fabsf(innerAngle);
    const float outer = outerMagnitude < minimum ? minimum
                                                 : (outerMagnitude > halfPi ? halfPi : outerMagnitude);
    float inner = innerMagnitude < minimum ? minimum
                                           : (innerMagnitude > halfPi ? halfPi : innerMagnitude);
    inner = inner > outer ? outer : inner;
    const float cosOuter = cosf(outer);
    const float cosInner = cosf(inner);
    const float difference = cosInner - cosOuter;
    const float scale = 1.0f / (difference > 1.0f / 1024.0f ? difference : 1.0f / 1024.0f);
    light.spot[0] = scale;
    light.spot[1] = -cosOuter * scale;
    light.spot[2] = 1.0f;
    culling.spot = true;
    culling.outerAngle = outer;
    return index;
}

inline void buildClusteredBuffers(const LightSet& set, Froxelizer& froxelizer, const float* view,
        ClusteredBuffers* out)
{
    froxelizer.froxelize(view, set.culling, set.count);
    memset(out, 0, sizeof(*out));
    memcpy(out->cluster.sunDirection, set.sunDirection, sizeof(set.sunDirection));
    memcpy(out->cluster.sunColorIntensity, set.sunColorIntensity, sizeof(set.sunColorIntensity));
    const FroxelParams& params = froxelizer.params();
    out->cluster.cells[0] = params.cellsXY[0];
    out->cluster.cells[1] = params.cellsXY[1];
    out->cluster.cells[2] = params.zLightFar;
    out->cluster.cells[3] = params.inverseLinearizer;
    out->cluster.counts[0] = static_cast<float>(params.countX);
    out->cluster.counts[1] = static_cast<float>(params.countY);
    out->cluster.counts[2] = static_cast<float>(params.countZ);
    out->cluster.counts[3] = static_cast<float>(set.count);
    memcpy(out->lights, set.lights, sizeof(out->lights));
    memcpy(out->froxels, froxelizer.entries(), sizeof(out->froxels));
    memcpy(out->records, froxelizer.records(), sizeof(out->records));
}

} // namespace zenapp
