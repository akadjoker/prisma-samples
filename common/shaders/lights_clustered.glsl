#include "lighting.glsl"

const int kMaxLights = 255;

layout(set = 0, binding = 3, std140) uniform Cluster
{
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
    vec4 uCells;
    vec4 uCounts;
};

layout(set = 0, binding = 4, std140) uniform Lights
{
    LightData uLights[kMaxLights + 1];
};

layout(set = 0, binding = 5, std140) uniform Froxels
{
    uvec4 uFroxels[1024];
};

layout(set = 0, binding = 6, std140) uniform Records
{
    uvec4 uRecords[1024];
};

uint froxelIndex(vec3 clipXyw)
{
    vec2 ndc = clipXyw.xy / clipXyw.z;
    vec2 cells = (ndc * 0.5 + 0.5) * uCells.xy;
    uint countX = uint(uCounts.x);
    uint countY = uint(uCounts.y);
    uvec2 cell = uvec2(clamp(cells, vec2(0.0), vec2(float(countX - 1u), float(countY - 1u))));
    float slice = log2(clipXyw.z / uCells.z) * uCells.w + uCounts.z;
    uint sliceZ = uint(clamp(slice, 0.0, uCounts.z - 1.0));
    return cell.x + cell.y * countX + sliceZ * countX * countY;
}

uint lightIndexAt(uint record)
{
    uint word = uRecords[record >> 4u][(record >> 2u) & 3u];
    return (word >> ((record & 3u) * 8u)) & 0xFFu;
}

vec3 evaluateLights(PbrSurface surface, vec3 n, vec3 v, vec3 worldPosition, vec3 clipXyw
#ifdef LIGHT_SHADOWS
        , vec3 geometricNormal
#endif
)
{
    vec3 color = vec3(0.0);
    if (uSunColorIntensity.w > 0.0)
        color += surfaceShading(surface, directionalLight(uSunDirection, uSunColorIntensity), n, v);

    uint index = froxelIndex(clipXyw);
    uint entry = uFroxels[index >> 2u][index & 3u];
    uint first = entry >> 16u;
    uint count = entry & 0xFFu;
    for (uint i = 0u; i < count; ++i)
    {
        LightData data = uLights[lightIndexAt(first + i)];
        Light light = punctualLight(data, worldPosition);
        if (light.attenuation <= 0.0 || dot(n, light.l) <= 0.0) continue;
#ifdef LIGHT_SHADOWS
        light.attenuation *= lightShadow(data.positionFalloff, data.direction, data.spot,
                worldPosition, geometricNormal, light.l);
#endif
        color += surfaceShading(surface, light, n, v);
    }
    return color;
}

vec3 evaluateAllLights(PbrSurface surface, vec3 n, vec3 v, vec3 worldPosition)
{
    vec3 color = vec3(0.0);
    if (uSunColorIntensity.w > 0.0)
        color += surfaceShading(surface, directionalLight(uSunDirection, uSunColorIntensity), n, v);
    int count = int(uCounts.w);
    for (int i = 0; i < count; ++i)
    {
        Light light = punctualLight(uLights[i], worldPosition);
        if (light.attenuation <= 0.0 || dot(n, light.l) <= 0.0) continue;
        color += surfaceShading(surface, light, n, v);
    }
    return color;
}
